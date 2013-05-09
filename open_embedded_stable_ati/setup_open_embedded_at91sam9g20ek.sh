#See below ##### for instructions on how to build with current settings
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


##### These instructions work for using current versions
#This gets your source directories
# Create a directory where you want your sources
mkdir -p ~/Angstrom/oe

#copy oe.tar where you want it.
cp oe.tar ~/Angstrom/oe
tar -xvf oe.tar

# Change to your open_embedded_stable_ati directory
cd ~/devel/atm/open_embedded_stable_ati

#Create sim links to everything
ln -s ~/Angstrom/oe/oe_sources oe_sources
ln -s ~/Angstrom/oe/openembedded openembedded
ln -s ~/Angstrom/oe/tmp tmp

#Change the TMPDIR to point at your home directory
vi oe_at91sam/conf/local.conf

#Build images
source ./oe_env.sh
bitbake console-image
bitbake u-boot

#Or use bitbake_build just make sure to change dirname to your home directory
vi bitbake_build
./bitbake_build





