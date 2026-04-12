BUILD_DIR := build/native
BUILD_STATIC := build/native_static
BUILD_ARM64 := build/arm64
BUILD_ARM64_ST := build/arm64_static
JOBS := $(shell nproc)

PREFIX ?= /usr/local
INSTALL_DIR := $(DESTDIR)$(PREFIX)/canscope

DEV_IMAGE   := canscope-dev
BUILD_DOCKER := build/docker
CROSS_IMAGE := canscope-cross
SYSROOT_IMAGE := canscope-sysroot
SYSROOT_CONTAINER := canscope-sysroot
DOCKER_SYSROOT := /sysroot
TOOLCHAIN := cmake/toolchain-aarch64.cmake
SSH_DIR ?= $(HOME)/.ssh
CMAKE_COMMON := -DFETCHCONTENT_UPDATES_DISCONNECTED=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

SSH_PREP := set -euo pipefail; \
	rm -rf /root/.ssh; \
	mkdir -p /root/.ssh; \
	cp -r /host_ssh/. /root/.ssh/; \
	chown -R $$(id -u):$$(id -g) /root/.ssh; \
	chmod 700 /root/.ssh; \
	find /root/.ssh -type f -exec chmod 600 {} \;; \
	[ -f /root/.ssh/known_hosts ] && chmod 644 /root/.ssh/known_hosts || true; \
	chmod 644 /root/.ssh/*.pub || true; \
	KEY=$$(find /root/.ssh -maxdepth 1 -type f \
		! -name "*.pub" \
		! -name "known_hosts" \
		! -name "authorized_keys" \
		! -name "config" | head -n1); \
	if [ -z "$$KEY" ]; then \
		echo "No SSH private key found in ~/.ssh"; \
		exit 1; \
	fi; \
	export GIT_SSH_COMMAND="ssh -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new -i $$KEY";

.PHONY: build build_static install install_static docker-run \
        build_arm64 build_arm64_static clean list

HOST_OS   := $(shell uname -s)
HOST_ARCH := $(shell uname -m)

build: ## Build $(HOST_ARCH) (dynamic linking)
	cmake -G Ninja -B $(BUILD_DIR) -S . $(CMAKE_COMMON) \
		-DBUILD_SHARED_LIBS=ON \
		-DCMAKE_INSTALL_RPATH='$$ORIGIN/../canscope/lib'
	cmake --build $(BUILD_DIR)

build_static: ## Build $(HOST_ARCH) (static linking)
	cmake -G Ninja -B $(BUILD_STATIC) -S . $(CMAKE_COMMON) \
		-DCMAKE_EXE_LINKER_FLAGS="-static" \
		-DBUILD_SHARED_LIBS=OFF \
		-DSTATIC=ON
	cmake --build $(BUILD_STATIC)

install: ## Install binary to PREFIX/bin, shared libs to PREFIX/canscope/lib
	@test -f $(BUILD_DIR)/canscope || { echo "Error: run 'make build' first"; exit 1; }
	install -d $(DESTDIR)$(PREFIX)/bin $(INSTALL_DIR)/lib
	install -m 755 $(BUILD_DIR)/canscope $(DESTDIR)$(PREFIX)/bin/
	patchelf --set-rpath '$$ORIGIN/../canscope/lib' $(DESTDIR)$(PREFIX)/bin/canscope
	find $(BUILD_DIR)/_deps -name '*.so*' -type f -exec install -m 755 {} $(INSTALL_DIR)/lib/ \;
	find $(BUILD_DIR)/_deps -name '*.so*' -type l -exec cp -a {} $(INSTALL_DIR)/lib/ \;

install_static: ## Install static binary to PREFIX/bin
	@test -f $(BUILD_STATIC)/canscope || { echo "Error: run 'make build_static' first"; exit 1; }
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BUILD_STATIC)/canscope $(DESTDIR)$(PREFIX)/bin/

docker-run: ## Build and run in Docker (works on Linux/Mac/Windows)
	docker build -t $(DEV_IMAGE) -f docker/Dockerfile.dev .
	docker run --rm -it \
		-v $(CURDIR):/app \
		-v $(SSH_DIR):/host_ssh:ro \
		-v /etc/hosts:/etc/hosts:ro \
		$(DEV_IMAGE) \
		bash -c '$(SSH_PREP) cmake -G Ninja -B $(BUILD_DOCKER) -S . $(CMAKE_COMMON) \
			&& cmake --build $(BUILD_DOCKER) \
			&& ./$(BUILD_DOCKER)/canscope $(ARGS)'

build_arm64: ## Cross-compile for arm64 (dynamic linking, in Docker)
	docker build -t $(CROSS_IMAGE) -f docker/Dockerfile.cross .
	docker build --platform=linux/arm64 -t $(SYSROOT_IMAGE) -f docker/Dockerfile.sysroot .
	-docker rm -f $(SYSROOT_CONTAINER) 2>/dev/null
	docker create --name $(SYSROOT_CONTAINER) $(SYSROOT_IMAGE)
	docker run --rm \
		-v $(CURDIR):/app \
		-v $(SSH_DIR):/host_ssh:ro \
		--volumes-from $(SYSROOT_CONTAINER) \
		$(CROSS_IMAGE) \
		bash -c '$(SSH_PREP) cmake -G Ninja -B $(BUILD_ARM64) -S . $(CMAKE_COMMON) \
			-DCMAKE_TOOLCHAIN_FILE=/app/$(TOOLCHAIN) \
			-DSYSROOT=$(DOCKER_SYSROOT) \
			&& cmake --build $(BUILD_ARM64)'

build_arm64_static: ## Cross-compile for arm64 (static linking, in Docker)
	docker build -t $(CROSS_IMAGE) -f docker/Dockerfile.cross .
	docker build --platform=linux/arm64 -t $(SYSROOT_IMAGE) -f docker/Dockerfile.sysroot .
	-docker rm -f $(SYSROOT_CONTAINER) 2>/dev/null
	docker create --name $(SYSROOT_CONTAINER) $(SYSROOT_IMAGE)
	docker run --rm \
		-v $(CURDIR):/app \
		-v $(SSH_DIR):/host_ssh:ro \
		--volumes-from $(SYSROOT_CONTAINER) \
		$(CROSS_IMAGE) \
		bash -c '$(SSH_PREP) cmake -G Ninja -B $(BUILD_ARM64_ST) -S . $(CMAKE_COMMON) \
			-DCMAKE_TOOLCHAIN_FILE=/app/$(TOOLCHAIN) \
			-DSYSROOT=$(DOCKER_SYSROOT) \
			-DCMAKE_EXE_LINKER_FLAGS=-static \
			-DBUILD_SHARED_LIBS=OFF \
			-DSTATIC=ON \
			&& cmake --build $(BUILD_ARM64_ST)'

clean: ## Remove all build directories
	rm -rf build

## Gets comments line and parses the name of target
list: ## Show available targets
	@grep -E '^[a-zA-Z0-9_-]+:.*##' $(MAKEFILE_LIST) | \
		sed 's/:.* ## /\t/' | sed 's/:.*##/\t/' | \
		sed 's/$$(HOST_ARCH)/$(HOST_ARCH)/g' | \
		awk -F '\t' '{printf "  \033[36m%-20s\033[0m %s\n", $$1, $$2}'
