
import os

"""
file_list = [os.path.join(root, name)
             for root, dirs, files in os.walk("./")
             for name in files
             if name.endswith((".txt", ".cfg"))]
"""

file_list = [os.path.join(root, name)
             for root, dirs, files in os.walk("./")
             for name in files
             if name.endswith((".txt"))]

print(file_list)

before_str_list = [
    "main.result_top_level_dir = results_2017", 
]
after_str_list = [
    "main.result_top_level_dir = results_2017_rate", 
]

# This is string replace
if(len(before_str_list) != len(after_str_list)):
    raise ValueError("Before and after value list must have the same length")


for name in file_list:
    fp = open(name, "r")
    s = fp.read()
    fp.close()
    for i in range(0, len(before_str_list)):
        before_str = before_str_list[i]
        after_str = after_str_list[i]
        s = s.replace(before_str, after_str)
    fp = open(name, "w")
    fp.write(s)


"""
for name in file_list:
    fp = open(name, "r")
    s = fp.read()
    fp.close()
    if(s[-1] != '\n'):
        s += '\n'
    if(s.find("chdir.chdir_is_rel") == -1):
        s += "chdir.chdir_is_rel = 1\n"
        fp = open(name, "w")
        fp.write(s)
        fp.close()
"""
"""
for name in file_list:
    fp = open(name, "r")
    s = fp.read()
    fp.close()
    if(s[-1] != '\n'):
        s += '\n'
    s += "main.result_top_level_dir = results_2017\n"
    fp = open(name, "w")
    fp.write(s)
    fp.close()
"""