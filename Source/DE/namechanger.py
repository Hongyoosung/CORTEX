import os
import re

def bulk_replace_keywords():
    # 1. 변환 매핑 정의 (Old -> New)
    replacements = {
        'GAMEAI_PROJECT_API' : 'DE_API'
    }

    # 2. 대상 디렉토리 및 확장자 설정
    target_dirs = ['public', 'private']
    target_extensions = ('.h', '.cpp')

    # 컴파일된 정규식 패턴 생성 (단어 경계 \b 사용)
    # 예: 'UTeamData'는 찾지만 'UTeamDataHelper'는 건드리지 않음
    patterns = {re.compile(rf'\b{old}\b'): new for old, new in replacements.items()}

    files_processed = 0
    changes_made = 0

    for target_dir in target_dirs:
        if not os.path.exists(target_dir):
            print(f"⚠️ 경고: '{target_dir}' 디렉토리를 찾을 수 없습니다. 건너뜁니다.")
            continue

        for root, _, files in os.walk(target_dir):
            for file in files:
                if file.endswith(target_extensions):
                    file_path = os.path.join(root, file)
                    
                    with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                        content = f.read()

                    new_content = content
                    file_changed = False

                    for pattern, replacement in patterns.items():
                        if pattern.search(new_content):
                            new_content = pattern.sub(replacement, new_content)
                            file_changed = True

                    if file_changed:
                        with open(file_path, 'w', encoding='utf-8') as f:
                            f.write(new_content)
                        print(f"✅ 변환 완료: {file_path}")
                        changes_made += 1
                    
                    files_processed += 1

    print("-" * 30)
    print(f"📊 작업 통계:")
    print(f"- 검사한 파일 수: {files_processed}")
    print(f"- 수정된 파일 수: {changes_made}")

if __name__ == "__main__":
    bulk_replace_keywords()