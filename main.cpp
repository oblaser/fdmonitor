/*
author          Oliver Blaser
date            03.09.2024
copyright       GPL-3.0 - Copyright (c) 2024 Oliver Blaser
*/

// g++ -std=c++17 -Wall -Werror=return-type -Werror=switch -Werror=format -Werror=reorder -pedantic -o fdmonitor main.cpp && ./fdmonitor (name | pid)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <dirent.h>
#include <unistd.h>


namespace fs = std::filesystem;

namespace util {

class FdTarget
{
public:
    FdTarget() = delete;

    FdTarget(const std::string& path, const std::string& type)
        : m_path(path), m_type(type)
    {}

    virtual ~FdTarget() {}

    const std::string& path() const { return m_path; }
    const std::string& type() const { return m_type; }

private:
    std::string m_path;
    std::string m_type;
};

class FileDescriptor
{
public:
    FileDescriptor() = delete;

    FileDescriptor(int32_t fd, const FdTarget& target)
        : m_fd(fd), m_target(target)
    {}

    FileDescriptor(int32_t fd, const std::string& path, const std::string& type)
        : m_fd(fd), m_target(path, type)
    {}

    virtual ~FileDescriptor() {}

    int32_t fd() const { return m_fd; }
    const FdTarget& target() const { return m_target; }

private:
    int32_t m_fd;
    FdTarget m_target;
};

class FdTargetCounter
{
public:
    FdTargetCounter() = delete;

    FdTargetCounter(const FdTarget& target, int32_t fd)
        : m_target(target), m_fd(1, fd)
    {}

    virtual ~FdTargetCounter() {}

    const FdTarget& target() const { return m_target; }
    size_t count() const { return m_fd.size(); }
    const std::vector<int32_t>& fd() const { return m_fd; }

    void add(int32_t fd) { m_fd.push_back(fd); }

private:
    FdTarget m_target;
    std::vector<int32_t> m_fd;
};

class FdContainer
{
public:
    FdContainer()
        : m_container()
    {}

    virtual ~FdContainer() {}

    void add(const FileDescriptor& target) { m_container.push_back(target); }

    std::vector<FdTargetCounter> getCounters() const;

private:
    std::vector<FileDescriptor> m_container;
};

std::string toString(const std::filesystem::file_type& fileType);

} // namespace util

bool operator==(const util::FdTarget& a, const util::FdTarget& b) { return ((a.path() == b.path()) && (a.type() == b.type())); }
bool operator!=(const util::FdTarget& a, const util::FdTarget& b) { return !(a == b); }


static pid_t getPid(const char* procName);
static void fdMonitor(pid_t pid);



int main(int argc, char** argv)
{
    if (argc == 2)
    {
        const char* const procName = argv[1];

        pid_t pid = 0;

        try
        {
            pid = std::stoi(procName);
        }
        catch (...)
        {
            pid = getPid(procName);
        }

        if (pid)
        {
            fdMonitor(pid);
            return 0;
        }
        else
        {
            printf("process \"%s\" not found\n", procName);
            return 1;
        }
    }
    else
    {
        printf("usage:\n\t%s (name | pid)\n", argv[0]);
        return 1;
    }
}



pid_t getPid(const char* procName)
{
    pid_t pid = 0;

    const char* const procDir = "/proc";
    size_t tmpProcNameSize = 300;
    char* tmpProcName = (char*)calloc(tmpProcNameSize, sizeof(char));

    DIR* dir = opendir(procDir);

    if (dir)
    {
        const struct dirent* e = readdir(dir);

        while (e)
        {
            const std::string name = e->d_name;

            try
            {
                const pid_t tmpPid = std::stoi(name);
                const fs::path cmdlineFilePath = fs::path(procDir) / std::to_string(tmpPid) / "cmdline";

                FILE* file = fopen(cmdlineFilePath.c_str(), "r");

                if (getline(&tmpProcName, &tmpProcNameSize, file) > 0)
                {
                    if (strcmp(tmpProcName, procName) == 0)
                    {
                        pid = tmpPid;
                        printf("found process \"%s\" with PID %i\n", procName, (int)pid);
                    }
                }

                fclose(file);
            }
            catch (...)
            {
                // nop, entry is not a numeric pid
                (void)0;
            }

            e = readdir(dir);
        }

        closedir(dir);
    }

    free(tmpProcName);

    return pid;
}

void fdMonitor(pid_t pid)
{
    const fs::path fdDirPath = "/proc/" + std::to_string(pid) + "/fd";

    util::FdContainer fileDescriptors;



    // fetch all open file descriptors
    for (const auto& entry : fs::directory_iterator(fdDirPath))
    {
        int32_t fd;

        try
        {
            fd = std::stoi(entry.path().filename().string());
        }
        catch (...)
        {
            printf("\033[91mentry \"%s\" is not a file descriptor\033[39m\n", entry.path().c_str());
            continue;
        }

        const std::string targetTypeStr = util::toString(entry.status().type());

        if (entry.is_symlink())
        {
            const fs::path targetPath = fs::read_symlink(entry.path());
            // printf("%i -> %s (%s)\n", fd, target.c_str(), targetTypeStr.c_str());

            fileDescriptors.add(util::FileDescriptor(fd, targetPath.string(), targetTypeStr));
        }
        else { printf("\033[91mentry \"%s\" (%s) is not a symlink\033[39m\n", entry.path().c_str(), targetTypeStr.c_str()); }
    }



    // print
    const auto fdCounters = fileDescriptors.getCounters();
    for (const auto& cntr : fdCounters)
    {
        const auto& fd = cntr.fd();
        const std::string targetStr = cntr.target().path() + " (" + cntr.target().type() + ")";

        printf("%-40s [%3i] ", targetStr.c_str(), (int)cntr.count());

        constexpr size_t maxNumFd = 7;
        size_t begin = 0;
        if (fd.size() > maxNumFd)
        {
            begin = fd.size() - maxNumFd;
            printf("...");
        }

        for (size_t i = begin; i < fd.size(); ++i)
        {
            if (i > 0) { printf(", "); }
            printf("%i", fd[i]);
        }

        printf("\n");
    }
}



namespace util {

std::vector<FdTargetCounter> FdContainer::getCounters() const
{
    std::vector<FdTargetCounter> r;

    // is regular file or directory
    auto isRegular = [](const std::string& type) { return ((type == toString(fs::file_type::regular)) || (type == toString(fs::file_type::directory))); };

    for (size_t i = 1; i < m_container.size(); ++i)
    {
        const auto& fd = m_container[i];

        bool found = false;

        for (auto& e : r)
        {
            std::error_code ec;


            if ((fd.target().type() == e.target().type()) &&
                ((fd.target().path() == e.target().path()) || (isRegular(fd.target().type()) && fs::equivalent(e.target().path(), fd.target().path(), ec))))
            {
                found = true;
                e.add(fd.fd());
            }
        }

        if (!found) { r.push_back(FdTargetCounter(fd.target(), fd.fd())); }
    }

    return r;
}

std::string toString(const std::filesystem::file_type& fileType)
{
    using type = std::filesystem::file_type;

    std::string str;

    switch (fileType)
    {
    case type::none:
        str = "none";
        break;

    case type::not_found:
        str = "not_found";
        break;

    case type::regular:
        str = "regular";
        break;

    case type::directory:
        str = "directory";
        break;

    case type::symlink:
        str = "symlink";
        break;

    case type::block:
        str = "block";
        break;

    case type::character:
        str = "character";
        break;

    case type::fifo:
        str = "fifo";
        break;

    case type::socket:
        str = "socket";
        break;

    case type::unknown:
        str = "unknown";
        break;
    }

    return str;
}

} // namespace util
