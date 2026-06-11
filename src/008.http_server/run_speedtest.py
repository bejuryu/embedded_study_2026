import os
import sys
import subprocess
import hashlib

if len(sys.argv) < 2:
    print("사용법: python run_speedtest.py [IP 또는 URL]")
    print("예시: python run_speedtest.py 192.168.1.2")
    print("예시: python run_speedtest.py https://esp32p4.local")
    sys.exit(1)

raw_target = sys.argv[1].strip()

# 프로토콜 접두사가 없는 경우 http:// 를 기본값으로 설정
if not raw_target.startswith("http://") and not raw_target.startswith("https://"):
    target_base = f"http://{raw_target}"
else:
    target_base = raw_target

target_base = target_base.rstrip("/")
target_url = f"{target_base}/api/files"

DUMMY_FILE = "speedtest_tmp_original.bin"
DOWNLOADED_FILE = "speedtest_tmp_downloaded.bin"

# 1. 100MB 무작위 더미 파일 생성
print("1. 100MB 더미 파일 생성 중...")
try:
    with open(DUMMY_FILE, "wb") as f:
        # 1MB씩 100번 루프 돌아서 쓰면 OOM을 방지하고 빠르게 생성 가능
        for _ in range(100):
            f.write(os.urandom(1024 * 1024))
except Exception as e:
    print(f"더미 파일 생성 실패: {e}")
    sys.exit(1)

def get_sha256(filepath):
    h = hashlib.sha256()
    try:
        with open(filepath, "rb") as f:
            for chunk in iter(lambda: f.read(65536), b""):
                h.update(chunk)
        return h.hexdigest()
    except Exception as e:
        print(f"해시 연산 실패 ({filepath}): {e}")
        return None

original_hash = get_sha256(DUMMY_FILE)
if not original_hash:
    sys.exit(1)

dev_null = "NUL" if os.name == "nt" else "/dev/null"

try:
    # 2. curl로 파일 업로드 속도 측정
    print("\n2. 파일 업로드 시작 (실시간 진행 상황 표시)...")
    upload_cmd = [
        "curl", "-k", "-o", dev_null,
        "-X", "POST",
        "-H", "Content-Type: application/octet-stream",
        "--data-binary", f"@{DUMMY_FILE}",
        f"{target_url}?path=/{DUMMY_FILE}"
    ]
    
    res_up = subprocess.run(upload_cmd)
    if res_up.returncode != 0:
        print(f"-> 업로드 실패 (cURL 에러 코드: {res_up.returncode})")
    else:
        print("-> 업로드 프로세스 완료")

    # 3. curl로 파일 다운로드 속도 측정
    print("\n3. 파일 다운로드 시작 (실시간 진행 상황 표시)...")
    download_cmd = [
        "curl", "-k", "-o", DOWNLOADED_FILE,
        f"{target_url}/content?path=/{DUMMY_FILE}"
    ]
    
    res_down = subprocess.run(download_cmd)
    if res_down.returncode != 0:
        print(f"-> 다운로드 실패 (cURL 에러 코드: {res_down.returncode})")
    else:
        print("-> 다운로드 프로세스 완료")

    # 4. 데이터 무결성 검증 (SHA-256 해시 비교)
    if os.path.exists(DOWNLOADED_FILE):
        print("\n4. 데이터 무결성 검증 중...")
        download_hash = get_sha256(DOWNLOADED_FILE)
        
        print(f"원본 파일 해시: {original_hash}")
        print(f"받은 파일 해시: {download_hash}")
        
        if original_hash == download_hash:
            print("★ 검증 성공: 업로드 및 다운로드한 파일이 100% 일치합니다. (무결성 통과)")
        else:
            print("❌ 검증 실패: 파일이 전송 도중 손상되었습니다. (무결성 실패)")
    else:
        print("\n4. 다운로드된 파일이 존재하지 않아 무결성 검증을 건너뜁니다.")

finally:
    # 5. 임시 파일 정리
    print("\n5. 임시 파일 정리 중...")
    if os.path.exists(DUMMY_FILE):
        os.remove(DUMMY_FILE)
    if os.path.exists(DOWNLOADED_FILE):
        os.remove(DOWNLOADED_FILE)
        
    # 타겟 장치의 원격 파일도 DELETE 메소드로 정리
    delete_cmd = [
        "curl", "-k", "-s", "-X", "DELETE",
        f"{target_url}?path=/{DUMMY_FILE}"
    ]
    subprocess.run(delete_cmd, capture_output=True)
    print("정리 완료.")
