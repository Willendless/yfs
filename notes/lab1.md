# lab1

## part1

## part2

### a

### 符号链接的实现

1. 在`fuse_lowlevel_ops`对象中注册`symlink`和`readlink`函数
2. `symlink`需要将路径存于符号链接文件中
3. `readlink`读取符号链接内容
4. 修改`getattr`函数，返回的属性对象中设置模式`st.st_mode  = S_IFLNK | 0777`，这样fuse就能够根据是文件、目录还是符号链接回调对应的函数
