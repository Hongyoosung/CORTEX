import os

def remove_keyword_from_files(root_dir, keyword=""):
    print(f"🚀 작업을 시작합니다. 대상 디렉토리: {os.path.abspath(root_dir)}")
    print(f"🔍 제거할 키워드: '{keyword}'\n")

    for root, dirs, files in os.walk(root_dir):
        for filename in files:
            # 1. 파일 이름에서 키워드 제거
            if keyword in filename:
                old_path = os.path.join(root, filename)
                new_filename = filename.replace(keyword, "")
                
                # 파일명 중간에 키워드가 있어 공백이나 언더바가 겹칠 경우 정리 (선택 사항)
                new_filename = new_filename.replace("__", "_").replace("--", "-")
                new_path = os.path.join(root, new_filename)

                try:
                    os.rename(old_path, new_path)
                    print(f"[파일명 변경] {filename} -> {new_filename}")
                    current_filename = new_filename
                except Exception as e:
                    print(f"❌ 파일명 변경 오류 ({filename}): {e}")
                    current_filename = filename
            else:
                current_filename = filename

            # 2. 파일 내용 내부의 키워드 제거 (필요한 경우만 사용)
            file_path = os.path.join(root, current_filename)
            try:
                # 텍스트 파일인지 확인하기 위해 인코딩 시도
                with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read()

                if keyword in content:
                    with open(file_path, 'w', encoding='utf-8') as f:
                        f.write(content.replace(keyword, ""))
                    print(f"[내용 수정] {current_filename}")
            except Exception as e:
                # 바이너리 파일(이미지, 실행파일 등)은 건너뜁니다.
                pass

    print("\n✅ 모든 작업이 완료되었습니다.")

if __name__ == "__main__":
    # 스크립트가 있는 현재 디렉토리를 기준으로 실행
    remove_keyword_from_files(".")