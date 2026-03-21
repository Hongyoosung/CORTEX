import os
import re

def bulk_replace_keywords():
    # 1. 변환 매핑 정의 (Old -> New)
    # 이제 부분 일치로 작동하므로 가장 구체적인 단어부터 적을 필요 없이 
    # 공통되는 키워드 위주로 정리하면 됩니다.
    replacements = {
        'Assault': 'Strike',
        'Defend': 'Vanguard',
        'assault' : 'strike',
        'defend' : 'vanguard',
        'Strategy' : 'Class',
        'strategy' : 'class',
        'Strategies' : 'Classes',
        'strategies' : 'classes'
    }

    # 2. 대상 디렉토리 및 확장자 설정
    target_dirs = ['public', 'private']
    target_extensions = ('.h', '.cpp')

    # \b (단어 경계)를 제거하여 'AssaultHelper' 내의 'Assault'도 찾도록 설정
    patterns = {re.compile(old): new for old, new in replacements.items()}

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
                    
                    try:
                        # 파일 읽기
                        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                            content = f.read()

                        new_content = content
                        file_changed = False

                        # 패턴 매칭 및 교체
                        for pattern, replacement in patterns.items():
                            if pattern.search(new_content):
                                # pattern.sub를 사용하여 해당 키워드가 포함된 모든 곳을 교체
                                new_content = pattern.sub(replacement, new_content)
                                file_changed = True

                        # 변경 사항이 있을 때만 파일 쓰기
                        if file_changed:
                            with open(file_path, 'w', encoding='utf-8') as f:
                                f.write(new_content)
                            print(f"✅ 변환 완료: {file_path}")
                            changes_made += 1
                        
                        files_processed += 1
                    except Exception as e:
                        print(f"❌ 에러 발생 ({file_path}): {e}")

    print("-" * 30)
    print(f"📊 작업 통계:")
    print(f"- 검사한 파일 수: {files_processed}")
    print(f"- 수정된 파일 수: {changes_made}")

if __name__ == "__main__":
    bulk_replace_keywords()