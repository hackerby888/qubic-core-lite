# What does it provide?

This Docker image provides a Ubuntu environment with ssh access. Mainly used for qubic.global if provider don't want the system access their host directly.

# Prerequisites

- Docker installed on your machine.

# How to use it?

One command to run the container:

```bash
# NOTE: run this part only once to build the image
# -----------------------------------
sudo apt update && apt install -y git
git clone https://github.com/hackerby888/qubic-core-lite.git
cd qubic-core-lite/docker/ubuntu-env
docker build -t qlite-env .
# -----------------------------------

# Run the container with SSH access
MY_QLITE_SSH_PASSWORD="$(tr -dc A-Za-z0-9 < /dev/urandom | head -c 16)"
docker run -d --name qlite-env-container -p 222:22 -p 21841:21841 -p 41841:41841 -p 21842:21842 -p 40420:40420 -e SSH_USERNAME=root -e SSH_PASSWORD=$MY_QLITE_SSH_PASSWORD qlite-env:latest

printf "\n################### IMPORTANT ###################\n"
echo "SSH access to the container is set up. Use the following credentials:"
echo "Username: root"
echo "Password: $MY_QLITE_SSH_PASSWORD"
echo "Connect using: ssh root@ip -p 222"
```

How to stop and remove the container:

```bash
docker stop qlite-env-container && docker rm qlite-env-container
```

# Port using

The following ports are exposed by the container:
- `222`: SSH access to the container.
- `21841`: Qubic P2P port.
- `41841`: Lite node http port.
- `21842`: Bob P2P port.
- `40420`: Bob node http port.

# Notes

- Make sure to change the SSH port mapping (`-p 222:22`) if port 222 is already in use on your host machine.
- You can customize the username and password by changing the `SSH_USERNAME` and `SSH_PASSWORD` environment variables in the `docker run` command.
- For security reasons, consider using SSH keys instead of passwords for authentication in a production environment. (by add `-e AUTHORIZED_KEYS="your_public_key"` to the `docker run` command)
