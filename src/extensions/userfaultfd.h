#pragma once

class UserFaultFD {
public:
    UserFaultFD() {
        fd = syscall(SYS_userfaultfd, O_NONBLOCK);
        if (fd < 0) throw std::runtime_error("userfaultfd failed");

        uffdio_api api{ .api = UFFD_API };
        if (ioctl(fd, UFFDIO_API, &api) == -1)
            throw std::runtime_error("UFFDIO_API failed");
    }

    ~UserFaultFD() { if (fd >= 0) close(fd); }

    int get() const { return fd; }

private:
    int fd;
};
