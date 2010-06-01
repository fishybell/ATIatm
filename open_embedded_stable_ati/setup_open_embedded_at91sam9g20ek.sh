# get bitbake, extract and create a symbolic link
wget http://download.berlios.de/bitbake/bitbake-1.8.18.tar.gz
tar zvxf bitbake-1.8.18.tar.gz
ln -s bitbake-1.8.18 bitbake 

# get sam-ba so we can program the board
wget http://www.atmel.com/dyn/resources/prod_documents/sam-ba_2.9_cdc_linux.zip 
unzip sam-ba_2.9_cdc_linux.zip
ln -s  sam-ba_cdc_2.9.linux_cdc_linux sam-ba

# clone OpenEmbedded stable
git clone git://git.openembedded.org/openembedded openembedded 
cd openembedded
git checkout -b stable_2009_ati origin/stable/2009
cd ..

source ./oe_env.sh
bitbake base-image
bitbake console-at91sam9-image




