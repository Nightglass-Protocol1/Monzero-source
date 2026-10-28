package=zstd
$(package)_version=1.5.7
$(package)_download_path=https://github.com/facebook/zstd/releases/download/v$($(package)_version)
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3

define $(package)_set_vars
$(package)_build_opts=CC="$($(package)_cc)"
$(package)_build_opts+=CFLAGS="$($(package)_cflags) $($(package)_cppflags) -fPIC"
$(package)_build_opts+=AR="$($(package)_ar)"
$(package)_build_opts+=RANLIB="$($(package)_ranlib)"
endef

define $(package)_build_cmds
  $(MAKE) -C lib $($(package)_build_opts) libzstd.a
endef

define $(package)_stage_cmds
  $(MAKE) -C lib DESTDIR=$($(package)_staging_dir) PREFIX=$(host_prefix) install-static $($(package)_build_opts)
endef
