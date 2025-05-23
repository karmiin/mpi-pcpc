FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV OMPI_ALLOW_RUN_AS_ROOT=1
ENV OMPI_ALLOW_RUN_AS_ROOT_CONFIRM=1

RUN apt-get update && apt-get install -y \
    build-essential \
    openmpi-bin \
    libopenmpi-dev \
    openssh-server \
    sshpass

# Crea la directory per ssh e imposta la password per root (ad esempio "password")
RUN mkdir /var/run/sshd && echo 'root:password' | chpasswd && \
    sed -i 's/#PermitRootLogin prohibit-password/PermitRootLogin yes/' /etc/ssh/sshd_config && \
    sed -i 's@session\s*required\s*pam_loginuid.so@session optional pam_loginuid.so@g' /etc/pam.d/sshd

WORKDIR /mpi
COPY . /mpi
RUN mpicc main.c -o main

# Avvia sshd e tieni il container attivo
CMD ["/usr/sbin/sshd", "-D"]