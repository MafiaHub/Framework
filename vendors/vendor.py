import requests
import zipfile
from os.path import join
from os import listdir, rmdir, remove
from shutil import move
from tqdm import tqdm

def download_file(url: str, fname: str, chunk_size=1024):
    resp = requests.get(url, stream=True)
    total = int(resp.headers.get('content-length', 0))
    with open(fname, 'wb') as file, tqdm(
        desc=fname,
        total=total,
        unit='iB',
        unit_scale=True,
        unit_divisor=1024,
    ) as bar:
        for data in resp.iter_content(chunk_size=chunk_size):
            size = file.write(data)
            bar.update(size)

def move_subfolder_to_parent(root: str, subfolder: str):
    for filename in listdir(join(root, subfolder)):
        move(join(root, subfolder, filename), join(root, filename))
    rmdir(join(root, subfolder))


def download_v8pp():
    version = "2.1.1"
    url = "https://github.com/pmed/v8pp/archive/refs/tags/v{}.zip".format(version)

    print("[V8PP] Downloading...")
    download_file(url, "v8pp.zip")

    print("[V8PP] Extracting...")
    with zipfile.ZipFile("v8pp.zip", 'r') as zip_ref:
        zip_ref.extractall("v8pp")

    print("[V8PP] Cleaning up...")
    remove("v8pp.zip")

    print("[V8PP] Moving subdirectory...")
    move_subfolder_to_parent("v8pp", "v8pp-{}".format(version))
    
    print("[V8PP] Finished")

def main():
    download_v8pp()

if __name__ == "__main__":
    main()