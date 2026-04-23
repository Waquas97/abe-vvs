
sudo apt update
sudo apt-get --assume-yes install libxml2-dev
wget https://repo.anaconda.com/miniconda/Miniconda3-latest-Linux-x86_64.sh
bash Miniconda3-latest-Linux-x86_64.sh

conda create -n rfsr python=3.10 -y
conda activate rfsr
pip install torch torchvision --index-url https://download.pytorch.org/whl/cu118
pip install numpy scipy scikit-learn joblib pykdtree plyfile

sudo apt-get --assume-yes install nvidia-driver-535-server nvidia-utils-535-server
sudo reboot


