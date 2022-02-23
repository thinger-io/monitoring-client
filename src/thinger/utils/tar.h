#pragma once

#include <filesystem>
#include <vector>

#include <archive.h>
#include <archive_entry.h>

#include <pwd.h>
#include <grp.h>

namespace Tar {

    namespace {

        void write_archive(const std::string source_path, const std::string outname, const std::vector<std::string> filename, const bool compression) {
            struct archive *a;
            struct archive_entry *entry;
            struct stat st;
            char buff[8192];
            int len;
            int fd;

            // It writes and compresses on disk
            a = archive_write_new();
            if (compression)
                archive_write_add_filter_gzip(a);
            archive_write_set_format_pax_restricted(a);
            archive_write_open_filename(a, outname.c_str());
            for (std::string f : filename ){
                stat(f.c_str(), &st);
                entry = archive_entry_new();
                //TODO: relative path does not work: https://stackoverflow.com/questions/65100774/how-to-create-a-libarchive-archive-on-a-directory-path-instead-of-a-list-of-file
                //archive_entry_set_pathname(entry, f.erase(0, source_path.size()+1).c_str());
                archive_entry_set_pathname(entry, f.c_str());
                archive_entry_copy_stat(entry, &st); // copies all file attributes
                archive_write_header(a, entry);
                fd = open(f.c_str(), O_RDONLY);
                len = read(fd, buff, sizeof(buff));
                while ( len > 0 ) {
                    archive_write_data(a, buff, len);
                    len = read(fd, buff, sizeof(buff));
                }
                close(fd);
                archive_entry_free(entry);
            }
            archive_write_close(a);
            archive_write_free(a);
        }

        int copy_data(struct archive *ar, struct archive *aw) {
             int r;
             const void *buff;
             size_t size;
             la_int64_t offset;

             for (;;) {
               r = archive_read_data_block(ar, &buff, &size, &offset);
               if (r == ARCHIVE_EOF)
                 return (ARCHIVE_OK);
               if (r < ARCHIVE_OK)
                 return (r);
               r = archive_write_data_block(aw, buff, size, offset);
               if (r < ARCHIVE_OK) {
                 fprintf(stderr, "%s\n", archive_error_string(aw));
                 return (r);
               }
            }
        }

        void extract_archive(const std::string file) {
            // I believe extraction happens in the same folder
            // at the moment we are fine as paths inside archive contain
            // full path. See: https://github.com/libarchive/libarchive/issues/1531

            // Decompression is done in disk: https://github.com/libarchive/libarchive/wiki/Examples#a-complete-extractor

            struct archive *a;
            struct archive *ext;
            struct archive_entry *entry;
            int flags;
            int r;

            /* Select which attributes we want to restore. */
            flags = ARCHIVE_EXTRACT_TIME;
            flags |= ARCHIVE_EXTRACT_PERM;
            flags |= ARCHIVE_EXTRACT_ACL;
            flags |= ARCHIVE_EXTRACT_FFLAGS;
            flags |= ARCHIVE_EXTRACT_OWNER;

            a = archive_read_new(); // reads file to decompress from disk
            archive_read_support_format_tar(a);
            //archive_read_support_compression_all(a); makes binary smaller
            archive_read_support_filter_gzip(a);
            ext = archive_write_disk_new(); // writes into disk
            archive_write_disk_set_options(ext, flags);
            archive_write_disk_set_standard_lookup(ext);
            if ((r = archive_read_open_filename(a, file.c_str(), 10240)))
              exit(1);
            for (;;) {
              r = archive_read_next_header(a, &entry);
              if (r == ARCHIVE_EOF)
                break;
              if (r < ARCHIVE_OK)
                fprintf(stderr, "%s\n", archive_error_string(a));
              if (r < ARCHIVE_WARN)
                exit(1);
              r = archive_write_header(ext, entry);
              if (r < ARCHIVE_OK)
                fprintf(stderr, "%s\n", archive_error_string(ext));
              else if (archive_entry_size(entry) > 0) {
                r = copy_data(a, ext);
                if (r < ARCHIVE_OK)
                  fprintf(stderr, "%s\n", archive_error_string(ext));
                if (r < ARCHIVE_WARN)
                  exit(1);
              }
              r = archive_write_finish_entry(ext);
              if (r < ARCHIVE_OK)
                fprintf(stderr, "%s\n", archive_error_string(ext));
              if (r < ARCHIVE_WARN)
                exit(1);
            }
            archive_read_close(a);
            archive_read_free(a);
            archive_write_close(ext);
            archive_write_free(ext);
        }

    }

    int create(const std::string source_path, const std::string dest_file) {

        bool compression = false;
        if (dest_file.substr(dest_file.find_last_of(".") + 1) == "tgz" ||
            dest_file.substr(dest_file.find_last_of(".") + 1) == "gz")
        {
            compression = true;
        }

        // Add file or recursive directory file given a full path
        std::vector<std::string> filename;
        if ( fs::is_directory(source_path) ) {
            for(auto& p: fs::recursive_directory_iterator(source_path)) {
                filename.push_back(p.path()); // add directories and regular files
            }
        } else {
            filename.push_back(source_path);
        }

        write_archive(source_path, dest_file, filename, compression);

        return 0;

    }

    // Global extractor
    int extract(const std::string file) {

        // TODO: should not need to copy to root folder, but we needed it until the relative path is saved into the file
        extract_archive(file);

        return 0;
    }

};
