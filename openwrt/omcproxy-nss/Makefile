#
# omcproxy with Qualcomm NSS multicast offload (qca-mcs)
#
# This is free software, licensed under the Apache License 2.0.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=omcproxy-nss
PKG_VERSION:=2026.11.18
PKG_RELEASE:=1

PKG_MAINTAINER:=ocformula <ocformula@proton.me>
PKG_LICENSE:=Apache-2.0
PKG_LICENSE_FILES:=LICENSE

include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/cmake.mk

# The sources are in the same repository as this package
SRC_ROOT:=$(realpath $(CURDIR)/../..)

define Package/omcproxy-nss
  SECTION:=net
  CATEGORY:=Network
  DEPENDS:=+libubox +libubus +kmod-qca-mcs
  TITLE:=IGMPv3 and MLDv2 Multicast Proxy with NSS multicast offload
  URL:=https://github.com/ocformula/omcproxy-nss
  PROVIDES:=omcproxy
  CONFLICTS:=omcproxy
endef

define Package/omcproxy-nss/description
 omcproxy that also programs the qca-mcs snooping tables of bridged
 downlinks, so that the Qualcomm NSS ECM can offload the forwarded
 multicast streams.
endef

define Package/omcproxy-nss/conffiles
/etc/config/omcproxy
endef

CMAKE_OPTIONS += -DWITH_LIBUBOX=1

define Build/Prepare
	$(CP) $(SRC_ROOT)/CMakeLists.txt $(SRC_ROOT)/LICENSE $(SRC_ROOT)/src $(PKG_BUILD_DIR)/
endef

define Package/omcproxy-nss/install
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/omcproxy.config $(1)/etc/config/omcproxy
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/omcproxy.init $(1)/etc/init.d/omcproxy
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/omcproxy $(1)/usr/sbin/
endef

$(eval $(call BuildPackage,omcproxy-nss))
