ipsec auto --up road
# ping will fail until we fix  up-client-v6 and add source address.
ping6 -c 2 -w 5 -I 2001:db8:0:3:1::0 2001:db8:0:2::254
echo done
