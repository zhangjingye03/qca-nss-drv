#
# Copyright (C) 2008 OpenWrt.org
#
# This is free software, licensed under the GNU General Public License v2.
# See /LICENSE for more information.
#

include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=qca-nss-drv
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define KernelPackage/qca-nss-drv
  SECTION:=kernel
  CATEGORY:=Kernel modules
  SUBMENU:=Qualcomm Network Sub System (NSS)
  TITLE:=Qualcomm NSS driver
  FILES:=$(PKG_BUILD_DIR)/qca-nss-drv.ko
  KCONFIG:=
  AUTOLOAD:=$(call AutoLoad,32, qca-nss-drv)
  DEPENDS:=@TARGET_ipq806x
endef

define KernelPackage/qca-nss-drv/Default/description
 This package contains the proprietary driver for Qualcomm Network Subsystem (NSS)
 chipset.
endef

EXTRA_KCONFIG:= \
	CONFIG_QCA-NSS-DRV=m

EXTRA_CFLAGS:= \
	$(patsubst CONFIG_%, -DCONFIG_%=1, $(patsubst %=m,%,$(filter %=m,$(EXTRA_KCONFIG)))) \
	$(patsubst CONFIG_%, -DCONFIG_%=1, $(patsubst %=y,%,$(filter %=y,$(EXTRA_KCONFIG)))) \
	-I$(PKG_BUILD_DIR)/nss_hal/include -I$(PKG_BUILD_DIR)/nss_hal/ipq806x \
	-I$(PKG_BUILD_DIR)/gmac \
	-DNSS_DEBUG_LEVEL=0 -DNSS_EMPTY_BUFFER_SIZE=1792 \
	-DNSS_PKT_STATS_ENABLED=0 -DNSS_CONNMGR_DEBUG_LEVEL=0 -DNSS_CONNMGR_PPPOE_SUPPORT=0 \
	-DNSS_TUNIPIP6_DEBUG_LEVEL=0 -DNSS_PM_DEBUG_LEVEL=0 -DNSSQDISC_DEBUG_LEVEL=0

MAKE_OPTS:= \
	ARCH="$(LINUX_KARCH)" \
	CROSS_COMPILE="$(TARGET_CROSS)" \
	SUBDIRS="$(PKG_BUILD_DIR)" \
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
	$(EXTRA_KCONFIG)

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) -C "$(LINUX_DIR)" \
		$(MAKE_OPTS) \
		modules
endef

$(eval $(call KernelPackage,qca-nss-drv))
