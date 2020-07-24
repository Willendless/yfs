// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

yfs_client::yfs_client()
{
    ec = new extent_client();

}

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
    ec = new extent_client();
    if (ec->put(1, "") != extent_protocol::OK)
        printf("error init root dir\n"); // XYB: init root dir
}

yfs_client::inum
yfs_client::n2i(std::string n)
{
    std::istringstream ist(n);
    unsigned long long finum;
    ist >> finum;
    return finum;
}

std::string
yfs_client::filename(inum inum)
{
    std::ostringstream ost;
    ost << inum;
    return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_FILE) {
        printf("isfile: %lld is a file\n", inum);
        return true;
    } 
    printf("isfile: %lld is a dir\n", inum);
    return false;
}
/** Your code here for Lab...
 * You may need to add routines such as
 * readlink, issymlink here to implement symbolic link.
 * 
 * */

bool
yfs_client::isdir(inum inum)
{
    // Oops! is this still correct when you implement symlink?
    return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
    int r = OK;

    printf("getfile %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    fin.atime = a.atime;
    fin.mtime = a.mtime;
    fin.ctime = a.ctime;
    fin.size = a.size;
    printf("getfile %016llx -> sz %llu\n", inum, fin.size);

release:
    return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
    int r = OK;

    printf("getdir %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }
    din.atime = a.atime;
    din.mtime = a.mtime;
    din.ctime = a.ctime;

release:
    return r;
}


#define EXT_RPC(xx) do { \
    if ((xx) != extent_protocol::OK) { \
        printf("EXT_RPC Error: %s:%d \n", __FILE__, __LINE__); \
        r = IOERR; \
        goto release; \
    } \
} while (0)

// Only support set size of attr
int
yfs_client::setattr(inum ino, size_t size)
{
    int r = OK;

    /*
     * your code goes here.
     * note: get the content of inode ino, and modify its content
     * according to the size (<, =, or >) content length.
     */

    return r;
}

int
yfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    int r = OK;

    printf("> yfs_client::create: parent inum: %016llx, file name: %s\n", parent, name);
    /*
     * your code goes here.
     * note: lookup is what you need to check if file exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */

    // check if file exists
    bool found;
    inum _inum;
    yfs_client::dirent new_dirent;
    std::string directory_content;

    lookup(parent, name, found, _inum);
    if (found) {
        r = EXIST;
        goto release;
    }

    if (ec->create(extent_protocol::T_FILE, ino_out)
            != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    // add dirent to parent
    new_dirent.name = std::string(name);
    new_dirent.inum = ino_out;
    if (ec->get(parent, directory_content) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }
    directory_content += new_dirent.dirent_disk();

    std::cout << "yfs_client::create finish: directory_content: "
              << directory_content << std::endl;
    if (ec->put(parent, directory_content) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

release:
    return r;
}

int
yfs_client::mkdir(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    int r = OK;

    printf("> yfs_client::mkdir parent inum: %016llx, file name: %s\n", parent, name);
    /*
     * your code goes here.
     * note: lookup is what you need to check if directory exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */

    return r;
}

int
yfs_client::lookup(inum parent, const char *name, bool &found, inum &ino_out)
{
    int r = OK;
    std::string fname(name);
    std::list<dirent> list;
    std::list<dirent>::iterator it;
    printf("> yfs_client::lookup parent inum: %016llx, file name: %s\n", parent, name);

    /*
     * your code goes here.
     * note: lookup file from parent dir according to name;
     * you should design the format of directory content.
     */
    if ((r = readdir(parent, list)) != OK) {
        return r;
    }

    for (it = list.begin(); it != list.end(); ++it) {
        if (fname == it->name) {
            found = true;
            ino_out = it->inum;
            return r;
        }
    }

    found = false;
    printf("> yfs_client::lookup: finish\n");
    return r;
}

int
yfs_client::readdir(inum dir, std::list<dirent> &list)
{
    int r = OK;
    printf("> yfs_client::readdir: dir: %016llx\n", dir);

    /*
     * your code goes here.
     * note: you should parse the dirctory content using your defined format,
     * and push the dirents to the list.
     */

    unsigned int i = 0;
    std::string buf;
    if (ec->get(dir, buf) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    // parse directory
    std::cout << "directory content: " << buf << std::endl;
    while (i < buf.size()) {
        unsigned int dirent_size = 0;
        unsigned int name_size   = 0;
        int l = i, r;
        dirent dirent;
        // read dirent size
        while (buf[i++] != ' ') ;
        r = i - 1;
        std::istringstream is1(buf.substr(l, r - l));
        is1 >> dirent_size;
        // read name size
        l = i;
        while (buf[i++] != '/') ;
        r = i - 1;
        std::istringstream is2(buf.substr(l, r - l));
        is2 >> name_size;

        std::cout << i << " " << dirent_size
                  << " " << name_size << std::endl;
        dirent.name = buf.substr(i, name_size);
        i += name_size;
        dirent.inum = n2i(buf.substr(i, dirent_size - name_size));
        i += dirent_size - name_size;
        list.push_back(dirent);
    }

release:
    printf("> yfs_client::readir: finish\n");
    return r;
}

int
yfs_client::read(inum ino, size_t size, off_t off, std::string &data)
{
    int r = OK;

    printf("> yfs_client::read: ino: %016llx\n", ino);


    /*
     * your code goes here.
     * note: read using ec->get().
     */

    return r;
}

int
yfs_client::write(inum ino, size_t size, off_t off, const char *data,
        size_t &bytes_written)
{
    int r = OK;

    printf("> yfs_client::write: ino: %016llx\n", ino);
    /*
     * your code goes here.
     * note: write using ec->put().
     * when off > length of original file, fill the holes with '\0'.
     */

    return r;
}

int yfs_client::unlink(inum parent,const char *name)
{
    int r = OK;

    /*
     * your code goes here.
     * note: you should remove the file using ec->remove,
     * and update the parent directory content.
     */

    return r;
}
