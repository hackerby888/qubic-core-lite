#!/bin/bash

# 1. Defaults
: ${SSH_USERNAME:=ubuntu}
: ${SSH_PASSWORD:?"Error: SSH_PASSWORD environment variable is not set."}

# 2. Fix PAM (Crucial for Ubuntu containers)
# This prevents the "session optional pam_loginuid.so" error
sed -i 's@session\s*required\s*pam_loginuid.so@session optional pam_loginuid.so@g' /etc/pam.d/sshd

# 3. Create User
if ! id "$SSH_USERNAME" &>/dev/null; then
    useradd -ms /bin/bash "$SSH_USERNAME"
    echo "$SSH_USERNAME:$SSH_PASSWORD" | chpasswd
    echo "User $SSH_USERNAME created."
fi

# 4. Also set Root password (so you can login as root if needed)
echo "root:$SSH_PASSWORD" | chpasswd
echo "My root credentials: root:$SSH_PASSWORD"

# 5. Enable Root Password Login (Moved outside the IF block)
sed -i 's/#PermitRootLogin.*/PermitRootLogin yes/' /etc/ssh/sshd_config
sed -i 's/#PasswordAuthentication.*/PasswordAuthentication yes/' /etc/ssh/sshd_config

# 6. Set Authorized Keys
if [ -n "$AUTHORIZED_KEYS" ] && [ -n "$SSH_USERNAME" ]; then
    USER_HOME=$(getent passwd "$SSH_USERNAME" | cut -d: -f6) || exit 1

    mkdir -p "$USER_HOME/.ssh"

    printf "%s\n" "$AUTHORIZED_KEYS" | tr -d '\r' > "$USER_HOME/.ssh/authorized_keys"

    chmod 700 "$USER_HOME/.ssh"
    chmod 600 "$USER_HOME/.ssh/authorized_keys"

    chown -R "$SSH_USERNAME:$SSH_USERNAME" "$USER_HOME/.ssh" 2>/dev/null || true
fi

# 7. Start SSH
echo "Starting SSH server on port 22..."
mkdir -p /var/run/sshd
exec /usr/sbin/sshd -D -e