conda activate rfsr
################# for working the minimal C with py init#################

conda install gcc -y
conda install -c conda-forge libxcrypt -y



############ABE tools and streaming client#############################
conda install flex -y
conda install bison -y
conda install -c conda-forge \
    libxml2 libcurl openssl glib icu \
    pkg-config gmp -y
    
########### PBC #######################
wget http://crypto.stanford.edu/pbc/files/pbc-0.5.14.tar.gz
tar xf pbc-0.5.14.tar.gz
cd pbc-0.5.14
./configure && make && sudo make install
cd ..
    
# Create a symlink for the library
ln -s /usr/local/lib/libpbc.so $CONDA_PREFIX/lib/libpbc.so
ln -s /usr/local/lib/libpbc.so.1 $CONDA_PREFIX/lib/libpbc.so.1

# Create a symlink for the headers
sudo ln -s /usr/local/include/pbc $CONDA_PREFIX/include/pbc

#####################################################


###################3 Libswabe##############################

wget http://acsc.cs.utexas.edu/cpabe/libbswabe-0.9.tar.gz
tar xf libbswabe-0.9.tar.gz
cd libbswabe-0.9
./configure && make && sudo make install
cd ..

# Link the library file
ln -sf /usr/local/lib/libbswabe.a $CONDA_PREFIX/lib/libbswabe.a
ln -sf /usr/local/lib/libbswabe.so $CONDA_PREFIX/lib/libbswabe.so

# Link the header file
ln -sf /usr/local/include/bswabe.h $CONDA_PREFIX/include/bswabe.h
##################################################################


#####################cpabe toolkit #################################

wget http://acsc.cs.utexas.edu/cpabe/cpabe-0.11.tar.gz
tar xf cpabe-0.11.tar.gz
cd cpabe-0.11
./configure 

#fix bugs in makefile and policy_lang.y
sed -i '/-lglib-2.0 \\/a -Wl,--copy-dt-needed-entries \\' Makefile
sed -i '/result: policy { final_policy = $1 }/s/$1 }/$1; }/' policy_lang.y

make && sudo make install
cd ..
#########################################################################




######################### stream client make #############################

############FIX OPENSSL ERROR##################
# Ensure the directory exists in Conda
mkdir -p $CONDA_PREFIX/include/openssl

# Create an EMPTY macros.h file in Conda
# This satisfies the #include without pulling in OpenSSL 3.0 logic
touch $CONDA_PREFIX/include/openssl/macros.h
################################################

##################3 fix lxml error################
# First, find what version you HAVE
ls $CONDA_PREFIX/lib/libxml2.so*

# Then, link it (replace .16 with whatever version you saw above)
ln -sf $CONDA_PREFIX/lib/libxml2.so.16 $CONDA_PREFIX/lib/libxml2.so
########################################################3

##########################################################################3



######################################################################################

#get streaming client
wget -L "https://raw.githubusercontent.com/Waquas97/abe-vvs/master/ABR-Inference_PC-Stream/ABR-Inference_PC-Stream.tar.xz"
tar -xf ABR-Inference_PC-Stream.tar.xz
rm ABR-Inference_PC-Stream.tar.xz
cd build
make
cd ..
mv build/ client-1/
cp -r client-1 client-2


