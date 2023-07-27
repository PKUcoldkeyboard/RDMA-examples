#/bin/bash

set -e

# Check if is ubuntu
if [ ! -f /etc/os-release ]; then
    echo "This script only works on Ubuntu"
    exit 1
fi

# Install dependencies
sudo apt-get install -y libibverbs1 ibverbs-utils librdmacm1 libibumad3 ibverbs-providers \
    rdma-core librdmacm-dev libibverbs-dev iproute2 perftest

# Install rxe
sudo modprobe rdma_rxe

# Configure rxe
sudo rdma link add rxe_0 type rxe netdev eth0

# Check rxe
rdma link
