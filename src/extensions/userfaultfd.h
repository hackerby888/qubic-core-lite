#pragma once

class UserFaultFD {
public:
    UserFaultFD() {
        fd = syscall(SYS_userfaultfd, O_NONBLOCK);
        if (fd < 0) throw std::runtime_error("Error: userfaultfd syscall failed, make sure you have root privileges | Line: " + std::to_string(__LINE__));

        uffdio_api api{ .api = UFFD_API };
        if (ioctl(fd, UFFDIO_API, &api) == -1)
            throw std::runtime_error("Error: UFFDIO_API ioctl failed | Line: " + std::to_string(__LINE__));
    }

    ~UserFaultFD() { if (fd >= 0) close(fd); }

    int get() const { return fd; }

private:
    int fd;
};
