# Libreswan library

# Copyright (C) 1998-2001  Henry Spencer.
# Copyright (C) 2003, 2004 Xelerance Corporation
# Copyright (C) 2012 Paul Wouters <pwouters@redhat.com>
# Copyright (C) 2012-2013 Paul Wouters <paul@libreswan.org>
# Copyright (C) 2015-2019 Andrew Cagney
# Copyright (C) 2019 Tuomo Soini <tis@foobar.fi>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2 of the License, or (at your
# option) any later version.  See <https://www.gnu.org/licenses/gpl2.txt>.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.

#
# XXX:
#
# Because libswan's Makefile tests USE_... variables, defined in
# config.mk, using "ifeq", it needs to include that file early.
#
# Instead:
#
# - replace the code with '$(call if-enabled)'.
#
# - move the optional files to their own library?
#

ifndef top_srcdir
include ../../mk/dirs.mk
endif
include $(top_srcdir)/mk/config.mk

LIB = libswan.a

OBJS += x509dn.o asn1.o oid.o

OBJS += constants.o \
	alloc.o diag.o \
	id.o initaddr.o initsubnet.o \
        keyblobtoid.o \
	lex.o \
	lswconf.o \
	lswfips.o \
	rangetosubnet.o sameaddr.o \
	secrets.o subnettot.o \
	subnettypeof.o ttoaddr.o ttodata.o ttoprotoport.o \
	ttosa.o ttosubnet.o ttoul.o \
	secitem_chunk.o \
	base64_pubkey.o \
	lswnss.o \
	lsw_passert_fail.o \
	alg_byname.o

OBJS += certs.o
OBJS += reqid.o

ifeq ($(USE_NETKEY),true)
OBJS += addr_lookup.o
endif

OBJS += log_ip.o
OBJS += fd.o
OBJS += kernel_alg.o
OBJS += kernel_sadb.o
OBJS += role.o

OBJS += addrtot.o
OBJS += addrtypeof.o
OBJS += anyaddr.o
OBJS += datatot.o
OBJS += satot.o
OBJS += ultot.o

OBJS += proposals.o
OBJS += v1_proposals.o
OBJS += v2_proposals.o
OBJS += esp_info.o
OBJS += ah_info.o
OBJS += ike_info.o

OBJS += ckaid.o

OBJS += chunk.o
OBJS += shunk.o

OBJS += ip_address.o
OBJS += ip_endpoint.o
OBJS += ip_info.o
OBJS += ip_protocol.o
OBJS += ip_range.o
OBJS += ip_said.o
OBJS += ip_subnet.o

OBJS += lmod.o
OBJS += lset.o
OBJS += deltatime.o
OBJS += realtime.o
OBJS += monotime.o

OBJS += refcnt.o
OBJS += debug.o
OBJS += impair.o
OBJS += keywords.o

OBJS += DBG_dump.o
OBJS += DBG_log.o

OBJS += libreswan_exit_log_errno.o
OBJS += libreswan_log_errno.o
OBJS += libreswan_bad_case.o

OBJS += libreswan_log.o
OBJS += libreswan_log_rc.o

OBJS += log_pexpect.o

OBJS += jambuf.o
OBJS += jam_bytes.o
OBJS += lswlog.o
OBJS += lswlog_dbg.o
OBJS += lswlog_nss_error.o
OBJS += lswlog_nss_ckm.o
OBJS += lswlog_nss_ckf.o
OBJS += lswlog_nss_cka.o
OBJS += lswlog_nss_secitem.o
OBJS += lswlog_enum_lset_short.o
OBJS += lswlog_realtime.o
OBJS += lswlog_monotime.o
OBJS += lswlog_to_file_stream.o
OBJS += lswlog_pexpect.o
OBJS += lswlog_passert.o

# Crypto code:
#
# XXX: More is coming as it gets cleaned up and made more library
# like.

# not much left using this
CFLAGS += -I$(top_srcdir)/lib/libcrypto

OBJS += ike_alg.o
OBJS += ike_alg_test.o
OBJS += ike_alg_dh_nss_ecp_ops.o
OBJS += ike_alg_dh_nss_modp_ops.o
OBJS += ike_alg_hash_nss_ops.o
OBJS += ike_alg_prf_mac_hmac_ops.o
OBJS += ike_alg_prf_mac_nss_ops.o
OBJS += ike_alg_prf_mac_xcbc_ops.o
OBJS += ike_alg_prf_ikev1_mac_ops.o
OBJS += ike_alg_prf_ikev2_mac_ops.o
ifeq ($(USE_NSS_PRF),true)
OBJS += ike_alg_prf_ikev1_nss_ops.o
OBJS += ike_alg_prf_ikev2_nss_ops.o
endif
OBJS += ike_alg_encrypt_chacha20_poly1305.o
OBJS += ike_alg_encrypt_nss_aead_ops.o
OBJS += ike_alg_encrypt_nss_cbc_ops.o
OBJS += ike_alg_encrypt_nss_ctr_ops.o
OBJS += ike_alg_encrypt_nss_gcm_ops.o
OBJS += ike_alg_desc.o
ifeq ($(USE_3DES),true)
OBJS += ike_alg_3des.o
endif
OBJS += ike_alg_aes.o
OBJS += ike_alg_camellia.o
ifeq ($(USE_CAST),true)
OBJS += ike_alg_cast.o
endif
OBJS += ike_alg_dh.o
OBJS += ike_alg_md5.o
OBJS += ike_alg_none.o
ifeq ($(USE_RIPEMD),true)
OBJS += ike_alg_ripemd.o
endif
ifeq ($(USE_SERPENT),true)
OBJS += ike_alg_serpent.o
endif
OBJS += ike_alg_sha1.o
OBJS += ike_alg_sha2.o
ifeq ($(USE_TWOFISH),true)
OBJS += ike_alg_twofish.o
endif

OBJS += crypt_mac.o

ifeq ($(USE_NSS_AVA_COPY),true)
OBJS += nss_copies.o
endif

ifeq ($(USE_UNBOUND_EVENT_H_COPY),true)
CFLAGS+=-I${LIBRESWANSRCDIR}/include/unbound
endif

ifeq ($(USE_DNSSEC),true)
OBJS += unbound.o
endif

ifeq ($(USE_KLIPS),true)
CFLAGS += -DKLIPS
endif

ifeq ($(USE_KLIPS),true)
CFLAGS += -DPFKEYV2
OBJS += pfkey_sock.o pfkey_error.o
OBJS += pfkey_v2_build.o pfkey_v2_ext_bits.o pfkey_v2_parse.o
OBJS += pfkey_v2_debug.o
endif

# Original flags; this is a mess.
CFLAGS+=${PORTINCLUDE} -I.
CFLAGS+=-I${LIBRESWANSRCDIR}
CFLAGS+= $(NSSFLAGS)
CFLAGS+=-I${LIBRESWANSRCDIR}/include ${PORTINCLUDE}
# XXX: hack until everything uses a consistent .c.o rule
CFLAGS+=$(USERLAND_CFLAGS)
CFLAGS+=${CROSSFLAGS}

OBJS += $(abs_builddir)/version.o

# build version.c using version number from Makefile.ver
$(abs_builddir)/version.c: $(srcdir)/version.in.c $(top_srcdir)/Makefile.ver
	rm -f $@.tmp
	sed -e '/"/s/@IPSECVERSION@/$(IPSECVERSION)/' \
	    -e '/"/s/@IPSECVIDVERSION@/$(IPSECVIDVERSION)/' \
	    $(srcdir)/version.in.c \
	    > $@.tmp
	mv $@.tmp $@

include $(top_srcdir)/mk/library.mk
