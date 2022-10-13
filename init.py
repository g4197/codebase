import os

master = "10.0.2.170"

machines = [
    '10.0.2.172', '10.0.2.173'
]

workspace = "/home/gh/"
dir_name = "RDMA-wrapper"

path = workspace + dir_name

os.system(
    f"parallel-ssh -h {','.join(machines)} -i 'mkdir {path} && mount {master}:{path} {path}'")
