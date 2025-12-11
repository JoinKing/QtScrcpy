import sys
import os
import re

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
            # 去掉 'v' 前缀
            tag_clean = tag[1:] if tag.startswith('v') else tag
            # 提取版本号部分（去掉可能的 -commit-g 后缀）
            # 例如: v1.2.3-4-g5a6b7c8 -> 1.2.3
            match = re.match(r'^(\d+\.\d+\.\d+)', tag_clean)
            if match:
                version = match.group(1)
            else:
                # 如果格式不标准，尝试提取所有数字部分
                parts = re.findall(r'\d+', tag_clean)
                if len(parts) >= 3:
                    version = '.'.join(parts[:3])
                elif len(parts) > 0:
                    # 如果只有部分数字，补齐为 0.0.0
                    version = "0.0.0"
    
    # CMake 要求版本号格式为 MAJOR.MINOR.PATCH，不能包含连字符
    # 如果没有有效的 tag，使用 0.0.0
    # 验证版本号格式
    if not re.match(r'^\d+\.\d+\.\d+$', version):
        version = "0.0.0"
    
    version_file = os.path.abspath(os.path.join(os.path.dirname(__file__), "../QtScrcpy/appversion"))
    file = open(version_file, 'w')
    file.write(version)
    file.close()
    sys.exit(0)