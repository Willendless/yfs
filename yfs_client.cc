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
    printf("isfile: %lld is not a file\n", inum);
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
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_DIR) {
        printf("isDir: %lld is a directory\n", inum);
        return true;
    } 
    printf("isDir: %lld is not a directory\n", inum);
    return false;
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
    extent_protocol::attr attr;
    std::string buf;

    /*
     * your code goes here.
     * note: get the content of inode ino, and modify its content
     * according to the size (<, =, or >) content length.
     */

    EXT_RPC(ec->getattr(ino, attr));

    if (attr.size == size) goto release;

    EXT_RPC(ec->get(ino, buf));

    if (attr.size < size) {
        EXT_RPC(ec->put(ino, buf.append(size - attr.size, '\0')));
    }

    if (attr.size > size) {
        EXT_RPC(ec->put(ino, buf.substr(0, size)));
    }

release:
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

    // create file
    if (mode == S_IFLNK)
        EXT_RPC(ec->create(extent_protocol::T_LINK, ino_out));
    else
        EXT_RPC(ec->create(extent_protocol::T_FILE, ino_out));


    // add dirent to parent
    new_dirent.name = name;
    new_dirent.inum = ino_out;
    EXT_RPC(ec->get(parent, directory_content));
    directory_content.append(new_dirent.dirent_disk());

    std::cout << "yfs_client::create finish: directory_content: "
              << directory_content << std::endl;
    
    EXT_RPC(ec->put(parent, directory_content));

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
    bool found;
    inum _;
    dirent new_dirent;
    std::string directory;

    EXT_RPC(lookup(parent, name, found, _));

    if (found) {
        r = EXIST;
        goto release;
    }

    EXT_RPC(ec->create(extent_protocol::T_DIR, ino_out));

    // add dirent to parent
    new_dirent.name = name;
    new_dirent.inum = ino_out;
    EXT_RPC(ec->get(parent, directory));
    directory.append(new_dirent.dirent_disk());
    EXT_RPC(ec->put(parent, directory));

release:
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
        goto release;
    }

    for (it = list.begin(); it != list.end(); ++it) {
        if (fname == it->name) {
            found = true;
            ino_out = it->inum;
            goto release;
        }
    }

    found = false;

release:
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
    EXT_RPC(ec->get(dir, buf));

    // parse directory
    std::cout << "directory content: " << buf << std::endl;
    while (i < buf.size()) {
        unsigned int dirent_size = 0;
        unsigned int name_size   = 0;
        dirent dirent;

        // read dirent size
        for (; buf[i] != ' '; ++i) {
            dirent_size = dirent_size * 10 + buf[i] - '0';
        }
        ++i;
        // read name size
        for (; buf[i] != '/'; ++i) {
            name_size = name_size * 10 + buf[i] - '0';
        }
        ++i;

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
    std::string buf;
    extent_protocol::attr attr;
    unsigned int off_to_end;
    unsigned int read_len;

    printf("> yfs_client::read: ino: %016llx, off: %ld, size: %lu\n", ino, off, size);

    /*
     * your code goes here.
     * note: read using ec->get().
     */

    data = "";

    EXT_RPC(ec->getattr(ino, attr));

    std::cout << "> read file's size: " << attr.size  << std::endl;

    EXT_RPC(ec->get(ino, buf));

    if (off > attr.size) {
        data = "";
        goto release;
    }

    std::cout << "> yfs_client::read:"
              << " origin size: " << buf.size()
              << " file content: " << buf << std::endl;

    off_to_end = buf.size() - off;
    read_len = off_to_end < size ? off_to_end : size;
    data = buf.substr(off, read_len);

    std::cout << "> yfs_client::read finish: size: " << data.size()
              << "read content " << data << std::endl;

release:
    return r;
}

int
yfs_client::write(inum ino, size_t size, off_t off, const char *data,
        size_t &bytes_written)
{
    int r = OK;
    std::string buf;
    std::string new_content(data);
    extent_protocol::attr attr;
    unsigned int enlarged_len;

    printf("> yfs_client::write: ino: %016llx, off: %lu, size: %lu", ino, off, size);
    /*
     * your code goes here.
     * note: write using ec->put().
     * when off > length of original file, fill the holes with '\0'.
     */
    bytes_written = 0;

    EXT_RPC(ec->getattr(ino, attr));

    EXT_RPC(ec->get(ino, buf));

    enlarged_len = off + size > attr.size ? 
                    off + size - attr.size :
                    0;
    
    std::cout << "> write: file old size: " << buf.size() << std::endl;
    std::cout << "> write: file old content: " << buf << std::endl;
    buf.append(enlarged_len, 0);
    buf.replace(off, size, data, size);
    std::cout << "> write: file new size: " << buf.size() << std::endl;

    EXT_RPC(ec->put(ino, buf));

    bytes_written = size;

    std::cout << "> write: file new size " << buf.size() << std::endl;
    std::cout << "> yfs_client::write finish: "
              << "file new size: " << buf.size()
              << "new file content: " << buf << std::endl;

release:
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
    inum inum;
    bool found;
    std::list<dirent> dirent_list;
    std::string buf;

    EXT_RPC(lookup(parent, name, found, inum));

    if (!found) {
        r = NOENT;
        goto release;
    }

    EXT_RPC(ec->remove(inum));

    EXT_RPC(readdir(parent, dirent_list));

    for (std::list<dirent>::iterator it = dirent_list.begin();
         it != dirent_list.end();
         ++it) {
        if (inum != it->inum) {
            buf.append(it->dirent_disk());
        }
    }

    EXT_RPC(ec->put(parent, buf));

release:
    return r;
}


int
yfs_client::symlink(inum parent, const char *name, const char *link)
{
    std::cout << "> yfs_client symlink: name : " << name << " link: " << link << std::endl;
    int r = OK;
    inum inum;
    std::string s(link);

    if (create(parent, name, S_IFLNK, inum) != OK) {
        r = EXIST;
        goto release;
    }

    EXT_RPC(ec->put(inum, s));

    std::cout << "> yfs_client symlink finish " << r << std::endl;

release:
    return r;
}

int
yfs_client::readlink(inum ino, std::string &data)
{

    std::cout << "> yfs_client readlink: " << std::endl;
    int r = OK;

    // check if file is a symlink ?

    if (isfile(ino) || isdir(ino)) {
        r = NOENT;
        goto release;
    }

    EXT_RPC(ec->get(ino, data));

    std::cout << "> yfs_client readlink finish: content: " << data << std::endl;

release:
    return r;
}
