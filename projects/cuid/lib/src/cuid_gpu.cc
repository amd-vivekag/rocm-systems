/*
 * Copyright (c) Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "cuid_gpu.h"
#include "cuid_file.h"
#include "cuid_util.h"
#include "pci_util.h"
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/types.h>
#include <unistd.h>

CuidGpu::CuidGpu(const amdcuid_gpu_info &i) : m_info(i) {}

// Helper to check if a /sys/class/drm entry name is a card device (e.g.,
// "card0", "card1") Excludes connector entries like "card0-DP-1" or
// "card0-HDMI-A-1"
static bool is_card_entry(const char *name) {
  if (strncmp(name, "card", 4) != 0 || !isdigit(name[4]))
    return false;
  for (size_t i = 4; name[i] != '\0'; ++i) {
    if (!isdigit(name[i]))
      return false;
  }
  return true;
}

// Extract the DRM node prefix and number from a sysfs device path.
// e.g., "/sys/class/drm/card5/device" -> ("card", 5)
// e.g., "/sys/class/drm/renderD128/device" -> ("renderD", 128)
static bool extract_drm_node_info(const std::string &device_path,
                                  std::string &prefix, int &number) {
  const std::string drm_prefix = "/sys/class/drm/";
  size_t drm_pos = device_path.find(drm_prefix);
  if (drm_pos == std::string::npos)
    return false;

  size_t name_start = drm_pos + drm_prefix.size();
  size_t name_end = device_path.find('/', name_start);
  if (name_end == std::string::npos)
    name_end = device_path.size();

  std::string node_name = device_path.substr(name_start, name_end - name_start);

  // Find where digits start in the node name
  size_t digit_start = 0;
  for (size_t i = 0; i < node_name.size(); ++i) {
    if (isdigit(node_name[i])) {
      digit_start = i;
      break;
    }
  }
  if (digit_start == 0)
    return false;

  prefix = node_name.substr(0, digit_start);
  number = std::stoi(node_name.substr(digit_start));
  return true;
}

// Resolve the renderD node path for a given card path.
// If a renderD node exists under the card's device/drm directory, returns
// "/sys/class/drm/renderDXXX". Otherwise, returns the original card path.
static std::string resolve_render_node(const std::string &card_path) {
  std::string drm_dir = card_path + "/device/drm";
  DIR *dir = opendir(drm_dir.c_str());
  if (dir) {
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
      if (strncmp(entry->d_name, "renderD", 7) == 0 &&
          isdigit(entry->d_name[7])) {
        std::string result = "/sys/class/drm/" + std::string(entry->d_name);
        closedir(dir);
        return result;
      }
    }
    closedir(dir);
  }
  return card_path;
}

amdcuid_status_t CuidGpu::discover(std::vector<DevicePtr> &gpus) {
  const char *drm_path = "/sys/class/drm";
  DIR *dir = opendir(drm_path);
  if (!dir)
    return AMDCUID_STATUS_UNSUPPORTED;

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    // Use card entries (e.g., card0, card1) which are always present for DRM
    // devices, unlike renderD nodes which may be absent with certain drivers
    // (e.g., GIM) or for non-AMD GPUs.
    if (is_card_entry(entry->d_name)) {
      std::string card_name(entry->d_name);
      std::string device_path =
          std::string(drm_path) + "/" + card_name + "/device";
      amdcuid_gpu_info info = {};
      amdcuid_status_t status = discover_single(&info, device_path);

      gpus.emplace_back(std::make_shared<CuidGpu>(info));
    }
  }
  closedir(dir);
  return AMDCUID_STATUS_SUCCESS;
}

std::string get_parent_device_path(const std::string &device_path,
                                   uint16_t unit_id) {
  if (unit_id == 0)
    return device_path;

  std::string prefix;
  int number = 0;
  if (extract_drm_node_info(device_path, prefix, number)) {
    int parent_number = number - unit_id;
    // Reconstruct the path with the parent DRM node number
    // e.g., /sys/class/drm/card5/device with unit_id=1 ->
    // /sys/class/drm/card4/device
    const std::string drm_prefix = "/sys/class/drm/";
    size_t name_start = device_path.find(drm_prefix) + drm_prefix.size();
    size_t name_end = device_path.find('/', name_start);
    if (name_end != std::string::npos) {
      return device_path.substr(0, name_start) + prefix +
             std::to_string(parent_number) + device_path.substr(name_end);
    }
  }
  return device_path;
}

amdcuid_status_t CuidGpu::discover_single(amdcuid_gpu_info *gpu_info,
                                          const std::string &device_path) {

  std::string device_path_in_use = device_path;
  amdcuid_gpu_info info = {};
  std::string bdf = CuidUtilities::readlink_bdf(device_path);
  info.header.fields.gpu.unit_id = 0;

  // If BDF is empty, this might be a partition device - try to get BDF from
  // parent and determine unit_id from the DRM node number difference
  if (bdf.empty()) {
    std::string node_prefix;
    int node_num = 0;
    if (extract_drm_node_info(device_path, node_prefix, node_num)) {
      const std::string drm_prefix = "/sys/class/drm/";
      size_t name_start = device_path.find(drm_prefix) + drm_prefix.size();
      size_t name_end = device_path.find('/', name_start);
      if (name_end != std::string::npos) {
        // Try parent DRM nodes (up to 7 partitions possible based on function
        // numbers 1-7)
        for (int offset = 1; offset <= 7; ++offset) {
          std::string parent_path =
              device_path.substr(0, name_start) + node_prefix +
              std::to_string(node_num - offset) + device_path.substr(name_end);
          std::string parent_bdf = CuidUtilities::readlink_bdf(parent_path);
          if (!parent_bdf.empty()) {
            // Found parent - unit_id is the offset, construct partition BDF
            info.header.fields.gpu.unit_id = static_cast<uint16_t>(offset);
            // Replace the function number in BDF with the unit_id
            size_t dot_pos = parent_bdf.rfind('.');
            if (dot_pos != std::string::npos) {
              bdf = parent_bdf.substr(0, dot_pos + 1) + std::to_string(offset);
            }
            device_path_in_use = parent_path;
            break;
          }
        }
      }
    }
  }
  // if bdf ends in non zero digit, then we have a partitioned device and we
  // will use that as unit id
  else {
    size_t last_dot = bdf.rfind('.');
    if (last_dot != std::string::npos && last_dot + 1 < bdf.size()) {
      char last_char = bdf[last_dot + 1];
      if (isdigit(last_char) && last_char != '0') {
        uint16_t unit_id = static_cast<uint16_t>(last_char - '0');
        info.header.fields.gpu.unit_id = unit_id;

        // partition device path will not have any of the other info, so we need
        // to get parent device path Calculate parent DRM node number by
        // subtracting unit_id from current node number e.g.,
        // /sys/class/drm/card5/device with unit_id=1 ->
        // /sys/class/drm/card4/device
        device_path_in_use = get_parent_device_path(device_path, unit_id);
      }
    }
  }

  std::string vendor =
      CuidUtilities::read_sysfs_file(device_path_in_use + "/vendor");
  // if this is a partitioned device, we won't be able to get information from
  // pci config space so skip in the check
  if (vendor.empty() && !bdf.empty() && info.header.fields.gpu.unit_id == 0) {
    // if file read fails, attempt to get from pci config
    uint8_t vendor_id_bytes[2] = {0};
    const uint16_t offset = 0x0;
    amdcuid_status_t status =
        PciUtil::read_pci_config_space(bdf, vendor_id_bytes, 2, offset);
    uint16_t vendor_id_int =
        PciUtil::le16_to_be16(*reinterpret_cast<uint16_t *>(vendor_id_bytes));
    info.header.fields.gpu.vendor_id =
        (status == AMDCUID_STATUS_SUCCESS) ? vendor_id_int : 0;
  } else {
    info.header.fields.gpu.vendor_id =
        (uint16_t)strtol(vendor.c_str(), nullptr, 16);
  }

  std::string device =
      CuidUtilities::read_sysfs_file(device_path_in_use + "/device");
  if (device.empty() && !bdf.empty() && info.header.fields.gpu.unit_id == 0) {
    // if file read fails, attempt to get from pci config
    uint8_t device_id_bytes[2] = {0};
    const uint16_t offset = 0x2;
    amdcuid_status_t status =
        PciUtil::read_pci_config_space(bdf, device_id_bytes, 2, offset);
    uint16_t device_id_int =
        PciUtil::le16_to_be16(*reinterpret_cast<uint16_t *>(device_id_bytes));
    info.header.fields.gpu.device_id =
        (status == AMDCUID_STATUS_SUCCESS) ? device_id_int : 0;
  } else {
    info.header.fields.gpu.device_id =
        (uint16_t)strtol(device.c_str(), nullptr, 16);
  }

  std::string pci_class =
      CuidUtilities::read_sysfs_file(device_path_in_use + "/class");
  uint16_t pci_class_integer = 0;
  if (pci_class.empty() && !bdf.empty() &&
      info.header.fields.gpu.unit_id == 0) {
    // if file read fails, attempt to get from pci config
    uint8_t class_id_bytes[2] = {0};
    const uint16_t offset = 0xa;
    amdcuid_status_t status =
        PciUtil::read_pci_config_space(bdf, class_id_bytes, 2, offset);
    uint16_t class_id_int =
        PciUtil::le16_to_be16(*reinterpret_cast<uint16_t *>(class_id_bytes));
    pci_class_integer = (status == AMDCUID_STATUS_SUCCESS) ? class_id_int : 0;
  } else {
    // sysfs class file returns 24-bit value (class:subclass:prog_if), shift
    // right by 8 to get 16-bit class:subclass
    pci_class_integer = (uint16_t)(strtol(pci_class.c_str(), nullptr, 16) >> 8);
  }
  info.header.fields.gpu.pci_class = pci_class_integer;

  std::string revision_id =
      CuidUtilities::read_sysfs_file(device_path_in_use + "/revision");
  if (revision_id.empty() && !bdf.empty() &&
      info.header.fields.gpu.unit_id == 0) {
    // if file read fails, attempt to get from pci config
    uint8_t revision_id_bytes[2] = {0};
    const uint16_t offset = 0x8;
    amdcuid_status_t status =
        PciUtil::read_pci_config_space(bdf, revision_id_bytes, 2, offset);
    uint16_t revision_id_int =
        PciUtil::le16_to_be16(*reinterpret_cast<uint16_t *>(revision_id_bytes));
    info.header.fields.gpu.revision_id =
        (status == AMDCUID_STATUS_SUCCESS) ? revision_id_int : 0;
  } else {
    info.header.fields.gpu.revision_id =
        (uint16_t)strtol(revision_id.c_str(), nullptr, 16);
  }

  // Determine the device node path. We prefer the renderD node for backward
  // compatibility (CUID files may reference renderD paths). If no renderD node
  // exists (e.g., GIM driver), the card path is used instead.
  std::string full_device_node;
  size_t last_slash = device_path.rfind('/');
  if (last_slash != std::string::npos && last_slash > 0) {
    // Trim to just /sys/class/drm/cardN or /sys/class/drm/renderDXXX
    full_device_node = device_path.substr(0, last_slash);
  } else {
    full_device_node = device_path;
  }

  // If discovered via card entry, try to resolve the associated renderD node
  if (full_device_node.find("/card") != std::string::npos) {
    std::string render_node = resolve_render_node(full_device_node);
    if (render_node != full_device_node) {
      full_device_node = render_node;
    }
  }

  info.header.device_type = AMDCUID_DEVICE_TYPE_GPU;
  info.bdf = bdf;
  info.render_node = full_device_node;

  *gpu_info = info;

  return AMDCUID_STATUS_SUCCESS;
}

amdcuid_status_t
CuidGpu::get_hardware_fingerprint(uint64_t &fingerprint) const {
  if (geteuid() != 0) {
    return AMDCUID_STATUS_PERMISSION_DENIED;
  }

  std::string unique_id_path = m_info.render_node + "/device/unique_id";
  // check if device is partition first by checking unit id
  if (m_info.header.fields.gpu.unit_id != 0) {
    unique_id_path = get_parent_device_path(unique_id_path,
                                            m_info.header.fields.gpu.unit_id);
  }
  // Try to read the unique_id from the device sysfs file
  std::ifstream fin(unique_id_path);
  if (fin.is_open()) {
    std::string hex_str;
    std::getline(fin, hex_str);
    fin.close();
    if (hex_str.empty()) {
      fingerprint = 0;
      return AMDCUID_STATUS_UNSUPPORTED;
    }
    // Parse as 64-bit hex value (if possible)
    try {
      fingerprint = std::stoull(hex_str, nullptr, 16);
    } catch (...) {
      fingerprint = 0;
      return AMDCUID_STATUS_UNSUPPORTED;
    }
  } else if (m_info.header.fields.gpu.unit_id == 0) {
    // attempt to get fingerprint through PCI Config Space if not a partition
    uint16_t offset = 0;
    amdcuid_status_t status =
        PciUtil::get_pci_cap_offset(m_info.bdf, 0x03, offset);
    if (status != AMDCUID_STATUS_SUCCESS) {
      return status;
    }

    uint8_t fingerprint_size = 8;
    uint8_t *fingerprint_buffer = new uint8_t[fingerprint_size];
    status = PciUtil::read_pci_config_space(m_info.bdf, fingerprint_buffer,
                                            fingerprint_size, offset);
    if (status != AMDCUID_STATUS_SUCCESS) {
      fingerprint = 0;
      delete[] fingerprint_buffer;
      return status;
    }
    // pcie config file is little endian, so need to convert to big endian
    fingerprint = PciUtil::le64_to_be64(
        *reinterpret_cast<uint64_t *>(fingerprint_buffer));
    delete[] fingerprint_buffer;
  } else {
    // partitioned device without unique_id file cannot get fingerprint
    fingerprint = 0;
    return AMDCUID_STATUS_UNSUPPORTED;
  }
  return AMDCUID_STATUS_SUCCESS;
}

amdcuid_status_t CuidGpu::get_primary_cuid(amdcuid_primary_id &id) const {
  if (geteuid() != 0) {
    return AMDCUID_STATUS_PERMISSION_DENIED;
  }

  // attempt to read the CUID from the file first
  std::string cuid_file_path = CuidUtilities::priv_cuid_file();
  CuidFile primary_file(cuid_file_path, false);
  primary_file.load();
  std::vector<CuidFileEntry> entries = primary_file.get_entries();

  CuidFileEntry entry;
  amdcuid_status_t status =
      primary_file.find_by_device_node(m_info.render_node, entry);
  if (status == AMDCUID_STATUS_SUCCESS) {
    id.UUIDv8_representation = entry.primary_cuid;
    CuidUtilities::remove_UUIDv8_bits(&id.UUIDv8_representation, id.raw_bits);
    return AMDCUID_STATUS_SUCCESS;
  }

  // primary CUID not found in file so generate it
  uint64_t fingerprint = 0;
  status = get_hardware_fingerprint(fingerprint);
  if (status != AMDCUID_STATUS_SUCCESS) {
    std::memset(&id, 0, sizeof(id));
    return status;
  }
  // Use header fields for the rest
  amdcuid_primary_id result = {};
  const auto &h = m_info.header;
  CuidUtilities::generate_primary_cuid(
      fingerprint, h.fields.gpu.unit_id, h.fields.gpu.revision_id,
      h.fields.gpu.device_id, h.fields.gpu.vendor_id,
      static_cast<uint8_t>(AMDCUID_DEVICE_TYPE_GPU), &result);

  id = result;
  return AMDCUID_STATUS_SUCCESS;
}

const amdcuid_gpu_info &CuidGpu::get_info() const { return m_info; }

amdcuid_status_t CuidGpu::get_vendor_id(uint16_t &vendor_id) const {
  vendor_id = m_info.header.fields.gpu.vendor_id;
  return AMDCUID_STATUS_SUCCESS;
}

amdcuid_status_t CuidGpu::get_device_id(uint16_t &device_id) const {
  device_id = m_info.header.fields.gpu.device_id;
  return AMDCUID_STATUS_SUCCESS;
}

amdcuid_status_t CuidGpu::get_pci_class(uint16_t &pci_class) const {
  pci_class = m_info.header.fields.gpu.pci_class;
  return AMDCUID_STATUS_SUCCESS;
}

amdcuid_status_t CuidGpu::get_revision_id(uint8_t &revision_id) const {
  revision_id = m_info.header.fields.gpu.revision_id;
  return AMDCUID_STATUS_SUCCESS;
}

amdcuid_status_t CuidGpu::get_unit_id(uint16_t &unit_id) const {
  unit_id = m_info.header.fields.gpu.unit_id;
  return AMDCUID_STATUS_SUCCESS;
}

amdcuid_status_t CuidGpu::get_bdf(std::string &bdf) const {
  if (m_info.bdf.empty()) {
    return AMDCUID_STATUS_UNSUPPORTED;
  }
  bdf = m_info.bdf;
  return AMDCUID_STATUS_SUCCESS;
}

amdcuid_status_t CuidGpu::get_device_path(std::string &path) const {
  if (m_info.render_node.empty()) {
    return AMDCUID_STATUS_UNSUPPORTED;
  }
  path = m_info.render_node;
  return AMDCUID_STATUS_SUCCESS;
}
