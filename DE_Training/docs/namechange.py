import os
import re

def replace_all_files_content():
    # 스크립트 위치 기준 경로
    base_dir = os.path.dirname(os.path.abspath(__file__))
    script_name = os.path.basename(__file__)
    
    # 대상 패턴: MOC, Moc, CORTEX, cortex
    # 이 중 하나라도 걸리면 DE로 바꿈
    pattern = re.compile(r'MOC|Moc|CORTEX|cortex')
    
    print(f"📂 작업 폴더: {base_dir}")
    print(f"🔄 대상: MOC, Moc, CORTEX, cortex -> DE (모든 텍스트 파일)\n")

    success_count = 0

    for filename in os.listdir(base_dir):
        file_path = os.path.join(base_dir, filename)

        # 파일이고, 자기 자신이 아니며, 확장자가 있는 경우(혹은 .md, .txt 등)
        if os.path.isfile(file_path) and filename != script_name:
            try:
                # 1. 파일 읽기 (인코딩 에러를 무시하여 최대한 많은 파일을 수용)
                with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read()

                # 2. 패턴 확인
                if pattern.search(content):
                    new_content = pattern.sub('DE', content)
                    
                    # 3. 변경 사항 저장
                    with open(file_path, 'w', encoding='utf-8', errors='ignore') as f:
                        f.write(new_content)
                    
                    print(f"✅ [수정완료] {filename}")
                    success_count += 1
                else:
                    print(f"➖ [변경없음] {filename}")

            except Exception as e:
                print(f"❌ [오류발생] {filename}: {e}")

    print(f"\n✨ 총 {success_count}개의 파일이 성공적으로 업데이트되었습니다.")

if __name__ == "__main__":
    replace_all_files_content()