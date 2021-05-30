#include "fat.hpp"

#include <cstring>
#include <cctype>
#include <utility>
#include "logger.hpp"

namespace {
std::pair<const char*, bool> NextPathElement(const char* path, char* path_elem) {
  const char* next_slash = strchr(path, '/');
  if(next_slash == nullptr) {
    strcpy(path_elem, path);
    return {nullptr, false};
  }

  const auto elem_len = next_slash - path;
  strncpy(path_elem, path, elem_len);
  path_elem[elem_len] = '\0';
  return {&next_slash[1], true};
}

}

namespace fat {
  BPB* boot_volume_image;
  unsigned long bytes_per_cluster;
  char current_path[30] = "/\0";

  void Initialize(void* volume_image) {
    boot_volume_image = reinterpret_cast<fat::BPB*>(volume_image);
    bytes_per_cluster =
      static_cast<unsigned long>(boot_volume_image->bytes_per_sector) *
      boot_volume_image->sectors_per_cluster;
    //current_path[0] = '/';
    //current_path[1] = '\0';
  }

  uintptr_t GetClusterAddr(unsigned long cluster) {
    unsigned long sector_num = 
      boot_volume_image->reserved_sector_count +
      boot_volume_image->num_fats * boot_volume_image->fat_size_32 + (cluster - 2) * boot_volume_image->sectors_per_cluster;
    uintptr_t offset = sector_num * boot_volume_image->bytes_per_sector;
    return reinterpret_cast<uintptr_t>(boot_volume_image) + offset;
  }

  void ReadName(const DirectoryEntry& entry, char* base, char* ext) {
    memcpy(base, &entry.name[0], 8);
    base[8] = 0;

    for(int i = 7; i >= 0 && base[i] == 0x20; i--) {
      base[i] = 0;
    }

    memcpy(ext, &entry.name[8], 3);
    ext[3] = 0;
    for(int i = 2; i >= 0 && ext[i] == 0x20; i--) {
      ext[i] = 0;
    }
  }

  unsigned long NextCluster(unsigned long cluster) {
    uintptr_t fat_offset = 
      boot_volume_image->reserved_sector_count * boot_volume_image->bytes_per_sector;
    uint32_t* fat = reinterpret_cast<uint32_t*>(
        reinterpret_cast<uintptr_t>(boot_volume_image) + fat_offset
        );
    uint32_t next = fat[cluster];
    if(next >= 0x0ffffff8ul) {
      return kEndOfClusterchain;
    }
    return next;
  }

  std::pair<DirectoryEntry*, bool> FindFile(const char* path, unsigned long directory_cluster) {
    if(path[0] == '/') {
      directory_cluster = boot_volume_image->root_cluster;
      path++;
    } else if(directory_cluster == 0) {
      directory_cluster = boot_volume_image->root_cluster;
    }

    char path_elem[13];
    auto [next_path, post_slash] = NextPathElement(path, path_elem);
    const bool path_last = next_path == nullptr || next_path[0] == '\0';

    while(directory_cluster != kEndOfClusterchain) {
      auto dir = GetSectorByCluster<DirectoryEntry>(directory_cluster);
      for(int i = 0; i < bytes_per_cluster / sizeof(DirectoryEntry); i++) {
        if(dir[i].name[0] == 0x00) {
          return {nullptr, post_slash};
        } else if(!NameIsEqual(dir[i], path_elem)) {
          continue;
        }

        if(dir[i].attr == Attribute::kDirectory && !path_last) {
          return FindFile(next_path, dir[i].FirstCluster());
        } else {
          /* dir[i] is not a directory or comes at the path last */
          return {&dir[i], post_slash};
        }
      }
      directory_cluster = NextCluster(directory_cluster);
    }

    return {nullptr, post_slash};
  }

  void ChangeDirectory(char* current_path, const char* dst_path) {
    if (dst_path == nullptr) {
      current_path[0] = '/';
      current_path[1] = '\0';
    } else if(dst_path[0] == '/') {
      const char* end_of_path = strchr(current_path, '\0');
      const auto path_len = end_of_path - current_path;
      strncpy(current_path, dst_path, path_len);
      if(current_path[path_len - 1] == '/') current_path[path_len - 1] = '\0';
    } else {
      /* current_path = /,  */
      strcat(current_path, dst_path);
      const char* end_of_path = strchr(current_path, '\0');
      const auto path_len = end_of_path - current_path;
      if(current_path[path_len - 1] == '/') current_path[path_len - 1] = '\0';
    }
  }

  bool NameIsEqual(const DirectoryEntry& entry, const char* name) {
    unsigned char name83[11];
    memset(name83, 0x20, sizeof(name83));

    int i = 0;
    int i83 = 0;
    for(; name[i] != 0 && i83 < sizeof(name83); i++, i83++) {
      if(name[i] == '.') {
        i83 = 7;
        continue;
      }
      name83[i83] = toupper(name[i]);
    }

    return memcmp(entry.name, name83, sizeof(name83)) == 0;
  }

  size_t LoadFile(void* buf, size_t len, const DirectoryEntry& entry) {
    auto is_valid_cluster = [](uint32_t c) {
      return c != 0 && c != fat::kEndOfClusterchain;
    };

    auto cluster = entry.FirstCluster();

    const auto buf_uint8 = reinterpret_cast<uint8_t*>(buf);
    const auto buf_end = buf_uint8 + len;
    auto p = buf_uint8;

    while(is_valid_cluster(cluster)) {
      if(bytes_per_cluster >= buf_end - p) {
        memcpy(p, GetSectorByCluster<uint8_t>(cluster), buf_end - p);
        return len;
      }
      memcpy(p, GetSectorByCluster<uint8_t>(cluster), bytes_per_cluster);
      p += bytes_per_cluster;
      cluster = NextCluster(cluster);
    }

    return p - buf_uint8;
  }

  void FormatName(const DirectoryEntry& entry, char* dest) {
    char ext[5] = ".";
    ReadName(entry, dest, &ext[1]);
    if(ext[1]){
      strcat(dest, ext);
    }
  }

  void GetAbsolutePath(char* current_path, const char* dst_path, char* abs_path) {
    /*
     * abs_path should be '/' or '/apps', '/apps/stars'
    */

    if(dst_path == nullptr) { /* abs_path -> current_path */
      strcpy(abs_path, current_path);
      return;
    }

    if(dst_path[0] == '/') {
      strcpy(abs_path, dst_path);
      return;
    }

    /* current_path is '/' or '/hoge' here */

    const char* end_of_current_path = strchr(current_path, '\0');
    const auto current_path_len = end_of_current_path - current_path;
    strncpy(abs_path, current_path, current_path_len);

    const char* end_of_relative_path = strchr(dst_path, '\0');
    const auto relative_path_len = end_of_relative_path - dst_path;

    if(current_path_len > 1) { /* /apps */
      abs_path[current_path_len] = '/';
      strncpy(abs_path + current_path_len + 1, dst_path, relative_path_len);
      abs_path[current_path_len + 1 + relative_path_len] = '\0';
    } else { /* / */
      strncpy(abs_path + current_path_len, dst_path, relative_path_len);
      abs_path[current_path_len + relative_path_len] = '\0';
    }
  }
}
