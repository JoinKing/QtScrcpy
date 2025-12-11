import sys
import os

if __name__ == '__main__':
    version = "0.0.0"
    
    # 尝试获取最新的 tag
    p = os.popen('git rev-list --tags --max-count=1')
    commit = p.read().strip()
    p.close()
    
    if commit:
        p = os.popen('git describe --tags ' + commit)
        tag = p.read().strip()
        p.close()
        
        if tag and len(tag) > 1:
            version = str(tag[1:]) if tag.startswith('v') else tag
    
    # 如果没有 tag，使用 commit hash 的前7位
    if version == "0.0.0":
        p = os.popen('git rev-parse --short HEAD')
        commit_hash = p.read().strip()
        p.close()
        if commit_hash:
            version = "0.0.0-" + commit_hash
    
    version_file = os.path.abspath(os.path.join(os.path.dirname(__file__), "../QtScrcpy/appversion"))
    file = open(version_file, 'w')
    file.write(version)
    file.close()
    sys.exit(0)