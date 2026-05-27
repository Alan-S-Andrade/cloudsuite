#!/usr/bin/env bash
set -euo pipefail

DOCKER_DATA_ROOT=${DOCKER_DATA_ROOT:-/myd/docker-data}
DOCKER_EXEC_ROOT=${DOCKER_EXEC_ROOT:-/myd/docker-exec}
DOCKER_TMPDIR=${DOCKER_TMPDIR:-/myd/docker-tmp}
CONTAINERD_ROOT=${CONTAINERD_ROOT:-/myd/containerd}

sudo mkdir -p "$DOCKER_DATA_ROOT" "$DOCKER_EXEC_ROOT" "$DOCKER_TMPDIR" \
  "$CONTAINERD_ROOT" /etc/docker /etc/systemd/system/docker.service.d

sudo tee /etc/docker/daemon.json >/dev/null <<EOF
{
  "data-root": "$DOCKER_DATA_ROOT",
  "exec-root": "$DOCKER_EXEC_ROOT"
}
EOF

sudo tee /etc/systemd/system/docker.service.d/myd-storage.conf >/dev/null <<EOF
[Service]
Environment="DOCKER_TMPDIR=$DOCKER_TMPDIR"
EOF

if [ -f /etc/containerd/config.toml ]; then
  sudo cp /etc/containerd/config.toml /etc/containerd/config.toml.bak
  sudo sed -i \
    -e "s#^root = .*#root = \"$CONTAINERD_ROOT\"#" \
    -e 's#^state = .*#state = "/run/containerd"#' \
    /etc/containerd/config.toml
fi

sudo systemctl daemon-reload
sudo systemctl restart docker

docker info --format 'Docker Root Dir: {{.DockerRootDir}}'
systemctl show docker -p Environment --value
