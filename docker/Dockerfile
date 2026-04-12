FROM manjarolinux/base:latest

RUN pacman -Syu --noconfirm && \
    pacman -S --noconfirm \
      clang \
      cmake \
      make \
      git \
      boost \
      zlib && \
    pacman -Scc --noconfirm

WORKDIR /app
COPY . .

RUN cmake -B build -S . && cmake --build build -j$(nproc)

ENTRYPOINT ["./build/canscope"]
