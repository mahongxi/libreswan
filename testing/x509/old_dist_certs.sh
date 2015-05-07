#!/bin/sh
#
#
# Copyright (C) 2007 Paul Wouters <paul@xelerance.com>
# Copyright (C) 2012 Paul Wouters <paul@libreswan.org>
#
# Setup a variety of X509 certificates for testcases
#
# ??? The following commonNames have two certificates generated.
# This is surely a bug.
#	bigkey.testing.libreswan.org
#	notvalidanymore.testing.libreswan.org
#	notyetvalid.testing.libreswan.org
#	signedbyotherca.testing.libreswan.org
#	wrongdnnum.testing.libreswan.org
#
# The problem was discovered through "set -ue"
# To mask the problem, we committed b0511b9817f8262ef1f5715066caa96c60e12e52

# u: reference to undefined variable is an error
# e: command returning error (outside of test) stops script
set -ue

cat <<EOF

   WARNING: This script doesn't work properly, run dist_certs.ph

EOF

# we are changing current directory to directory where command is located at
origindir="$(pwd)"
cd "$(dirname $0)"

if ! type touch >/dev/null
then
       echo "error: touch(1) needed for dist_certs, not found in path"
       exit 1
fi

if ! type expect >/dev/null
then
       echo "error: expect(1) needed for dist_certs, not found in path"
       exit 1
fi

# Clean
rm -rf reqs/* certs/* keys/* newcerts/* cacerts/* crls/* pkcs12/mainca/* pkcs12/otherca/* pkcs12/* pkcs12/west_root_ca/* index.txt* serial* nss-pw crlnumber

echo -n "foobar" > nss-pw
# Prep
mkdir -p certs crls newcerts keys reqs pkcs12/mainca pkcs12/otherca pkcs12/curveca cacerts
touch index.txt
echo "01" > serial
echo "01" > crlnumber
export OPENSSL_CONF=./openssl.cnf

# Get some useful dates. Blame openssl for not being Y2K compatible with its -startdate
TODAY="$(date +'%y%m%d')"
TWO_DAYS_AGO="$(date --date='2 days ago' +'%y%m%d')"
START="$TWO_DAYS_AGO"000000Z
END="$TWO_DAYS_AGO"235959Z
# future < 10*365 (length of CA validity)
FUTURE="$(date --date='1 year' +'%y%m%d')"000000Z
FUTURE_END="$(date --date='1 year' +'%y%m%d')"000000Z

echo "------------------------------------------"
echo "Certificate dates being used for this run:"
echo ""
echo "Today: $TODAY"
echo "Two days ago: $TWO_DAYS_AGO"
echo "Start: $START"
echo "End: $END"
echo "Future: $FUTURE"
echo "Future End: $FUTURE_END"
echo "------------------------------------------"
echo ""

# Generate CA's
for cauth in mainca otherca east_chain_root west_chain_root
do
openssl genrsa -passout pass:foobar -des3 -out keys/$cauth.key 1024
openssl rsa -in keys/$cauth.key -out keys/$cauth.key -passin pass:foobar
# use defaults to ensure the same values for all certs based on openssl.cnf
# req -x509 does not accept -startdate, this might invalidate certs?
expect  <<EOF
spawn openssl req -x509 -days 3650 -newkey rsa:1024 -keyout keys/$cauth.key -out cacerts/$cauth.crt -passin pass:foobar -passout pass:foobar
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "Libreswan test CA for $cauth\n"
expect "Email"
send "testing@libreswan.org\n"
wait
EOF
done


#Generate EC CA
openssl ecparam -out keys/curveca.key -name secp384r1 -genkey -noout
expect  <<EOF
spawn openssl req -x509 -new -key keys/curveca.key -out cacerts/curveca.crt -days 3650
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "Libreswan test EC CA\n"
expect "Email"
send "testing@libreswan.org\n"
wait
EOF

# Generate machine keys/certs
for machine in east west sunset sunrise north south road pole park beet carrot nic japan revoked notyetvalid notvalidanymore signedbyotherca spaceincn bigkey wrongdnorg unwisechar hashsha2 wrongdnnum cnofca
do
# generate host key/cert
expect  <<EOF
spawn openssl req -newkey rsa:1024 -passin pass:foobar -passout pass:foobar -keyout keys/$machine.key -out reqs/$machine.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "$machine.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF
# sign machine cert
openssl ca -batch -in reqs/$machine.req -startdate $START -enddate $FUTURE -out certs/$machine.crt -notext -cert cacerts/mainca.crt -keyfile keys/mainca.key  -passin pass:foobar
# package in pkcs#12
openssl pkcs12 -export -inkey keys/$machine.key -in certs/$machine.crt -name "$machine" -certfile cacerts/mainca.crt -caname "mainca" -out pkcs12/mainca/$machine.p12 -passin pass:foobar -passout pass:foobar
done

#Generate EC machine keys/certs
for machine in east west
do
openssl ecparam -out keys/$machine-ec.key -name secp384r1 -genkey -noout
expect  <<EOF
spawn openssl req -new -nodes -key keys/$machine-ec.key -out reqs/$machine-ec.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "$machine-ec.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF
# sign EC certs
openssl ca -batch -md sha1 -startdate $START -enddate $FUTURE -in reqs/$machine-ec.req -keyfile keys/curveca.key -cert cacerts/curveca.crt -out certs/$machine-ec.crt -notext
openssl pkcs12 -export -inkey keys/$machine-ec.key -in certs/$machine-ec.crt -name "$machine-ec" -certfile cacerts/curveca.crt -caname "curveca" -out pkcs12/curveca/$machine-ec.p12 -passin pass: -passout pass:
done

# Special Cases follow

# large rsa key
expect  <<EOF
spawn openssl req -newkey rsa:2048 -passin pass:foobar -passout pass:foobar -keyout keys/bigkey.key -out reqs/bigkey.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "bigkey.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF
openssl ca -batch -in reqs/bigkey.req -startdate $START -days 365 -out certs/bigkey.crt -notext -cert cacerts/mainca.crt -keyfile keys/mainca.key  -passin pass:foobar

# cert that is not yet valid
openssl ca -batch -in reqs/notyetvalid.req -startdate $FUTURE -enddate $FUTURE_END -out certs/notyetvalid.crt -notext -cert cacerts/mainca.crt -keyfile keys/mainca.key  -passin pass:foobar
openssl pkcs12 -export -inkey keys/notyetvalid.key -in certs/notyetvalid.crt -name "$machine" -certfile cacerts/mainca.crt -caname "otherca" -out pkcs12/otherca/notyetvalid.p12 -passin pass:foobar -passout pass:foobar

# cert not valid anymore
openssl ca -batch -in reqs/notvalidanymore.req -startdate $START -enddate $END -out certs/notvalidanymore.crt -notext -cert cacerts/mainca.crt -keyfile keys/mainca.key  -passin pass:foobar
openssl pkcs12 -export -inkey keys/notvalidanymore.key -in certs/notvalidanymore.crt -name "$machine" -certfile cacerts/mainca.crt -caname "otherca" -out pkcs12/otherca/notvalidanymore.p12 -passin pass:foobar -passout pass:foobar

# signed by other ca
rm -f certs/signedbyotherca.crt pkcs12/mainca/signedbyother.p12
openssl ca -batch -in reqs/signedbyotherca.req -startdate $START -days 365 -out certs/signedbyotherca.crt -notext -cert cacerts/otherca.crt -keyfile keys/otherca.key  -passin pass:foobar
openssl pkcs12 -export -inkey keys/signedbyotherca.key -in certs/signedbyotherca.crt -name "$machine" -certfile cacerts/otherca.crt -caname "otherca" -out pkcs12/otherca/signedbyotherca.p12 -passin pass:foobar -passout pass:foobar

# wrong DN (Organisation is different)
expect  <<EOF
spawn openssl req -newkey rsa:1024 -passin pass:foobar -passout pass:foobar -keyout keys/wrongdnorg.key -out reqs/wrongdnorg.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "Traitors Inc\n"
expect "Organizational"
send "\n"
expect "Common"
send "wrongdnorg.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF
openssl ca -batch -in reqs/wrongdnorg.req -startdate $START -days 365 -out certs/wrongdnorg.crt -notext -cert cacerts/mainca.crt -keyfile keys/mainca.key  -passin pass:foobar
openssl pkcs12 -export -inkey keys/wrongdnorg.key -in certs/wrongdnorg.crt -name "wrongdnorg" -certfile cacerts/mainca.crt -caname "mainca" -out pkcs12/mainca/wrongdnorg.p12 -passin pass:foobar -passout pass:foobar

# wrong number of DN's
expect  <<EOF
spawn openssl req -newkey rsa:1024 -passin pass:foobar -passout pass:foobar -keyout keys/wrongdnnum.key -out reqs/wrongdnnum.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send ".\n"
expect "Common"
send "wrongdnnum.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF
openssl ca -batch -in reqs/wrongdnnum.req -startdate $START -days 365 -out certs/wrongdnnum.crt -notext -cert cacerts/mainca.crt -keyfile keys/mainca.key  -passin pass:foobar
openssl pkcs12 -export -inkey keys/wrongdnorg.key -in certs/wrongdnorg.crt -name "wrongdnorg" -certfile cacerts/mainca.crt -caname "mainca" -out pkcs12/mainca/wrongdnorg.p12 -passin pass:foobar -passout pass:foobar

# Unwise charachters
expect  <<EOF
spawn openssl req -newkey rsa:1024 -passin pass:foobar -passout pass:foobar -keyout keys/unwisechar.key -out reqs/unwisechar.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "unwisechar ~!@#$%^&*()-_=+;:/?<>.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF
openssl ca -batch -in reqs/unwisechar.req -startdate $START -days 365 -out certs/unwisechar.crt -notext -cert cacerts/mainca.crt -keyfile keys/mainca.key  -passin pass:foobar
openssl pkcs12 -export -inkey keys/unwisechar.key -in certs/unwisechar.crt -name "unwisechar" -certfile cacerts/mainca.crt -caname "mainca" -out pkcs12/mainca/unwisechar.p12 -passin pass:foobar -passout pass:foobar

# Using SHA2
expect  <<EOF
spawn openssl req -newkey rsa:1024  -nodes -sha256 -passin pass:foobar -passout pass:foobar -keyout keys/hashsha2.key -out reqs/hashsha2.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "hashsha2\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF
openssl ca -batch -in reqs/hashsha2.req -startdate $START -days 365 -out certs/hashsha2.crt -notext -cert cacerts/mainca.crt -keyfile keys/mainca.key  -passin pass:foobar
openssl pkcs12 -export -inkey keys/hashsha2.key -in certs/hashsha2.crt -name "hashsha2" -certfile cacerts/mainca.crt -caname "mainca" -out pkcs12/mainca/hashsha2.p12 -passin pass:foobar -passout pass:foobar

# Space in CN
expect  <<EOF
spawn openssl req -newkey rsa:1024 -passin pass:foobar -passout pass:foobar -keyout keys/spaceincn.key -out reqs/spaceincn.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "space invaders.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF
openssl ca -batch -in reqs/spaceincn.req -startdate $START -days 365 -out certs/spaceincn.crt -notext -cert cacerts/mainca.crt -keyfile keys/mainca.key  -passin pass:foobar
openssl pkcs12 -export -inkey keys/spaceincn.key -in certs/spaceincn.crt -name "spaceincn" -certfile cacerts/mainca.crt -caname "mainca" -out pkcs12/mainca/spaceincn.p12 -passin pass:foobar -passout pass:foobar

# CN of client cert is the same as the CA's CN
expect  <<EOF
spawn openssl req -newkey rsa:1024 -passin pass:foobar -passout pass:foobar -keyout keys/cnofca.key -out reqs/cnofca.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "Libreswan test CA for mainca\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF
openssl ca -batch -in reqs/cnofca.req -startdate $START -days 365 -out certs/cnofca.crt -notext -cert cacerts/mainca.crt -keyfile keys/mainca.key  -passin pass:foobar
openssl pkcs12 -export -inkey keys/cnofca.key -in certs/cnofca.crt -name "cnofca" -certfile cacerts/mainca.crt -caname "mainca" -out pkcs12/mainca/cnofca.p12 -passin pass:foobar -passout pass:foobar

# Revoke and generate CRL
openssl ca -gencrl -crldays 15 -out crls/cacrlvalid.pem  -keyfile keys/mainca.key -cert cacerts/mainca.crt -passin pass:foobar
openssl ca -gencrl -startdate $START -enddate $END -out crls/cacrlexpired.pem  -keyfile keys/mainca.key -cert cacerts/mainca.crt -passin pass:foobar
openssl ca -gencrl -startdate $FUTURE -enddate $FUTURE_END -out crls/cacrlnotyetvalid.pem  -keyfile keys/mainca.key -cert cacerts/mainca.crt -passin pass:foobar
openssl ca -gencrl -crldays 15 -out crls/othercacrl.pem  -keyfile keys/otherca.key -cert cacerts/otherca.crt -passin pass:foobar
openssl ca -revoke certs/revoked.crt -keyfile keys/mainca.key -cert cacerts/mainca.crt -passin pass:foobar
openssl ca -gencrl -crldays 15 -out crls/cacrlvalid.pem -keyfile keys/mainca.key -cert cacerts/mainca.crt -passin pass:foobar

# now we will attempt to create a CRL with a signature that happens to start with 00
# As we need to change something to get a different signature, we change the validity time
echo "Attempting to generate a CRL with leading zero byte - this can take a minute"
days=1
while true
do
        openssl ca -gencrl -crldays $days -out crls/crl-leading-zero-byte.pem -keyfile keys/mainca.key -cert cacerts/mainca.crt -passin pass:foobar >/dev/null
        openssl crl -text -in crls/crl-leading-zero-byte.pem -noout | grep -A1 "Signature Algorithm:"|tail -1| sed "s/ //g" |grep ^00 > /dev/null
        RETVAL=$?
        if [ $RETVAL -eq 0 ]
        then
                echo "Found leading zero CRL signature for days=$days"
                break
        else
                days=$((days+1))
        fi;
done

# Make ROOT->I1->I2->I3->EE chains
# copy and paste warning!

machine=east_chain_intermediate_1
signer=east_chain_root
expect  <<EOF
spawn openssl req -newkey rsa:1024 -passin pass:foobar -passout pass:foobar -keyout keys/$machine.key -out reqs/$machine.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "$machine.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF

# sign machine cert
openssl ca -batch -in reqs/$machine.req -startdate $START -enddate $FUTURE -out cacerts/$machine.crt -notext -cert cacerts/$signer.crt -keyfile keys/$signer.key -passin pass:foobar -extensions v3_ca

machine=east_chain_intermediate_2
signer=east_chain_intermediate_1
expect  <<EOF
spawn openssl req -newkey rsa:1024 -passin pass:foobar -passout pass:foobar -keyout keys/$machine.key -out reqs/$machine.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "$machine.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF

# sign machine cert
openssl ca -batch -in reqs/$machine.req -startdate $START -enddate $FUTURE -out cacerts/$machine.crt -notext -cert cacerts/$signer.crt -keyfile keys/$signer.key -passin pass:foobar -extensions v3_ca

machine=east_chain_intermediate_3
signer=east_chain_intermediate_2
expect  <<EOF
spawn openssl req -newkey rsa:1024 -passin pass:foobar -passout pass:foobar -keyout keys/$machine.key -out reqs/$machine.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "$machine.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF

# sign machine cert
openssl ca -batch -in reqs/$machine.req -startdate $START -enddate $FUTURE -out cacerts/$machine.crt -notext -cert cacerts/$signer.crt -keyfile keys/$signer.key -passin pass:foobar -extensions v3_ca


machine=east_chain_endcert
signer=east_chain_intermediate_3
expect  <<EOF
spawn openssl req -newkey rsa:1024 -passin pass:foobar -passout pass:foobar -keyout keys/$machine.key -out reqs/$machine.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "$machine.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF

# sign machine cert
openssl ca -batch -in reqs/$machine.req -startdate $START -enddate $FUTURE -out certs/$machine.crt -notext -cert cacerts/$signer.crt -keyfile keys/$signer.key -passin pass:foobar
openssl pkcs12 -export -inkey keys/$machine.key -in certs/$machine.crt -name "$machine" -certfile cacerts/east_chain_root.crt -caname "east_chain_root" -out pkcs12/$machine.p12 -passin pass:foobar -passout pass:foobar

machine=west_chain_intermediate_1
signer=west_chain_root
expect  <<EOF
spawn openssl req -newkey rsa:1024 -passin pass:foobar -passout pass:foobar -keyout keys/$machine.key -out reqs/$machine.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "$machine.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF

# sign machine cert
openssl ca -batch -in reqs/$machine.req -startdate $START -enddate $FUTURE -out cacerts/$machine.crt -notext -cert cacerts/$signer.crt -keyfile keys/$signer.key -passin pass:foobar -extensions v3_ca

machine=west_chain_intermediate_2
signer=west_chain_intermediate_1
expect  <<EOF
spawn openssl req -newkey rsa:1024 -passin pass:foobar -passout pass:foobar -keyout keys/$machine.key -out reqs/$machine.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "$machine.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF

# sign machine cert
openssl ca -batch -in reqs/$machine.req -startdate $START -enddate $FUTURE -out cacerts/$machine.crt -notext -cert cacerts/$signer.crt -keyfile keys/$signer.key -passin pass:foobar -extensions v3_ca

machine=west_chain_intermediate_3
signer=west_chain_intermediate_2
expect  <<EOF
spawn openssl req -newkey rsa:1024 -passin pass:foobar -passout pass:foobar -keyout keys/$machine.key -out reqs/$machine.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "$machine.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF

# sign machine cert
openssl ca -batch -in reqs/$machine.req -startdate $START -enddate $FUTURE -out cacerts/$machine.crt -notext -cert cacerts/$signer.crt -keyfile keys/$signer.key -passin pass:foobar -extensions v3_ca


machine=west_chain_endcert
signer=west_chain_intermediate_3
expect  <<EOF
spawn openssl req -newkey rsa:1024 -passin pass:foobar -passout pass:foobar -keyout keys/$machine.key -out reqs/$machine.req
expect "Country Name"
send "\n"
expect "State"
send "\n"
expect "Locality"
send "\n"
expect "Organization"
send "\n"
expect "Organizational"
send "\n"
expect "Common"
send "$machine.testing.libreswan.org\n"
expect "Email"
send "testing@libreswan.org\n"
expect "challenge"
send "\n"
expect "optional"
send "\n"
wait
EOF

# sign machine cert
openssl ca -batch -in reqs/$machine.req -startdate $START -enddate $FUTURE -out certs/$machine.crt -notext -cert cacerts/$signer.crt -keyfile keys/$signer.key -passin pass:foobar
openssl pkcs12 -export -inkey keys/$machine.key -in certs/$machine.crt -name "$machine" -certfile cacerts/west_chain_root.crt -caname "west_chain_root" -out pkcs12/$machine.p12 -passin pass:foobar -passout pass:foobar

echo "X509 test material generation complete"
cd "${origindir}"
