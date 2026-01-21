# What does it provide?

This Docker image provides a Ubuntu environment with ssh access. Mainly used for qubic.global if provider don't want the system access their host directly.

# Prerequisites

- Docker installed on your machine.
- Git.

# How to use it?

One command to run the container:

```bash
git clone https://github.com/hackerby888/qubic-core-lite.git
cd qubic-core-lite/docker/ubuntu-env
docker build -t qlite-env .

MY_QLITE_SSH_PASSWORD="$(tr -dc A-Za-z0-9 < /dev/urandom | head -c 16)"
docker run -d -p 222:22 -e SSH_USERNAME=root -e SSH_PASSWORD=$MY_QLITE_SSH_PASSWORD qlite-env:latest

printf "\n################### IMPORTANT ###################\n"
echo "SSH access to the container is set up. Use the following credentials:"
echo "Username: root"
echo "Password: $MY_QLITE_SSH_PASSWORD"
echo "Connect using: ssh root@ip -p 222"
```

# Notes

- Make sure to change the SSH port mapping (`-p 222:22`) if port 222 is already in use on your host machine.
- You can customize the username and password by changing the `SSH_USERNAME` and `SSH_PASSWORD` environment variables in the `docker run` command.
- For security reasons, consider using SSH keys instead of passwords for authentication in a production environment. (by add `-e AUTHORIZED_KEYS="your_public_key"` to the `docker run` command)
