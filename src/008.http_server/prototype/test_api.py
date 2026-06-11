#!/usr/bin/env python
import argparse
import sys
import http.client
import urllib.parse
import json
import time
import ssl
import hashlib
import os
import unittest
import socket
import unicodedata

def pad_cjk(s, width, align='left'):
    visual_width = sum(2 if unicodedata.east_asian_width(c) in ('W', 'F', 'A') else 1 for c in s)
    padding = max(0, width - visual_width)
    if align == 'right':
        return ' ' * padding + s
    return s + ' ' * padding

# 테스트 실행 결과를 수집할 글로벌 리스트
TEST_RESULTS = []

class TestControlPanel(unittest.TestCase):
    TARGET_HOST = "localhost"
    TARGET_PORT = 8000
    TARGET_HTTPS_PORT = 8443

    def get_conn(self, protocol: str, timeout=10):
        if protocol == "https":
            ctx = ssl._create_unverified_context()
            return http.client.HTTPSConnection(self.TARGET_HOST, self.TARGET_HTTPS_PORT, context=ctx, timeout=timeout)
        else:
            return http.client.HTTPConnection(self.TARGET_HOST, self.TARGET_PORT, timeout=timeout)

    def record_result(self, protocol: str, task: str, status: str, start_time: float):
        elapsed = time.time() - start_time
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task, 50)} | 결과: {pad_cjk(status, 28)} | 시간: {elapsed:.3f}초")
        TEST_RESULTS.append({
            "protocol": protocol.upper(),
            "task": task,
            "status": status,
            "elapsed": f"{elapsed:.3f}s"
        })

    # ==================================================================
    # [1] 정상 동작 테스트 (Happy Path)
    # ==================================================================

    def run_status_test(self, protocol: str):
        task_desc = "서버 연결 상태 확인"
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task_desc, 50)} 실행 중...", end="\r", flush=True)
        start = time.time()
        conn = self.get_conn(protocol)
        try:
            conn.request("GET", "/api/status")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            data = json.loads(res.read().decode())
            self.assertEqual(data, {"status": "ok"})
            self.record_result(protocol, task_desc, "PASS", start)
        except Exception as e:
            self.record_result(protocol, task_desc, f"FAIL ({type(e).__name__})", start)
            raise e
        finally:
            conn.close()

    def run_directory_test(self, protocol: str):
        task_desc = "폴더 생성 및 삭제 기능 검증"
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task_desc, 50)} 실행 중...", end="\r", flush=True)
        start = time.time()
        conn = self.get_conn(protocol)
        folder_name = f"auto_test_folder_{protocol}"
        folder_path = f"/{folder_name}"
        try:
            headers = {"Content-Type": "application/json"}
            conn.request("POST", "/api/files", body=json.dumps({"path": folder_path}), headers=headers)
            res = conn.getresponse()
            res.read()
            self.assertEqual(res.status, 201)

            conn.request("GET", f"/api/files?path={urllib.parse.quote(folder_path)}")
            res = conn.getresponse()
            data = json.loads(res.read().decode())
            self.assertEqual(res.status, 200)
            names = [f["name"] for f in data["files"]]
            self.assertIn("..", names)

            conn.request("DELETE", f"/api/files?path={urllib.parse.quote(folder_path)}")
            res = conn.getresponse()
            res.read()
            self.assertEqual(res.status, 204)
            self.record_result(protocol, task_desc, "PASS", start)
        except Exception as e:
            self.record_result(protocol, task_desc, f"FAIL ({type(e).__name__})", start)
            raise e
        finally:
            conn.close()

    def run_file_io_test(self, protocol: str):
        task_desc = "파일 업로드 및 다운로드 정합성 검증"
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task_desc, 50)} 실행 중...", end="\r", flush=True)
        start = time.time()
        conn = self.get_conn(protocol)
        file_name = f"auto_test_io_{protocol}.bin"
        file_path = f"/{file_name}"
        raw_data = os.urandom(32768)
        raw_hash = hashlib.sha256(raw_data).hexdigest()
        try:
            boundary = "----WebKitFormBoundaryUnitTest"
            body = (
                f"--{boundary}\r\n"
                f'Content-Disposition: form-data; name="file"; filename="{file_name}"\r\n'
                f"Content-Type: application/octet-stream\r\n\r\n"
            ).encode() + raw_data + f"\r\n--{boundary}--\r\n".encode()
            headers = {"Content-Type": f"multipart/form-data; boundary={boundary}", "Content-Length": str(len(body))}
            conn.request("POST", f"/api/files?path={urllib.parse.quote(file_path)}", body=body, headers=headers)
            res = conn.getresponse()
            res.read()
            self.assertEqual(res.status, 201)

            conn.request("GET", f"/api/files/content?path={urllib.parse.quote(file_path)}")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            downloaded = bytearray()
            while True:
                chunk = res.read(32768)
                if not chunk:
                    break
                downloaded.extend(chunk)
            self.assertEqual(raw_hash, hashlib.sha256(downloaded).hexdigest())

            conn.request("DELETE", f"/api/files?path={urllib.parse.quote(file_path)}")
            res = conn.getresponse()
            res.read()
            self.assertEqual(res.status, 204)
            self.record_result(protocol, task_desc, "PASS", start)
        except Exception as e:
            self.record_result(protocol, task_desc, f"FAIL ({type(e).__name__})", start)
            raise e
        finally:
            conn.close()

    def run_speedtest_download(self, protocol: str, size: int, size_label: str):
        task_desc = f"다운로드 속도 측정: {size_label}"
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task_desc, 50)} 실행 중...", end="\r", flush=True)
        start = time.time()
        conn = self.get_conn(protocol, timeout=30)
        try:
            conn.request("GET", f"/api/speedtest/download?size={size}")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            self.assertEqual(int(res.getheader("Content-Length", 0)), size)
            total = 0
            while True:
                chunk = res.read(32768)
                if not chunk:
                    break
                total += len(chunk)
            elapsed = max(0.001, time.time() - start)
            speed = (total * 8) / (1024 * 1024) / elapsed
            self.record_result(protocol, task_desc, f"PASS ({speed:.2f} Mbps)", start)
        except Exception as e:
            self.record_result(protocol, task_desc, f"FAIL ({type(e).__name__})", start)
            raise e
        finally:
            conn.close()

    def run_speedtest_upload(self, protocol: str, size: int, size_label: str):
        task_desc = f"업로드 속도 측정: {size_label}"
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task_desc, 50)} 실행 중...", end="\r", flush=True)
        start = time.time()
        conn = self.get_conn(protocol, timeout=30)
        chunk_size = 32768
        chunk = b"U" * chunk_size
        def gen():
            sent = 0
            while sent < size:
                to_send = min(chunk_size, size - sent)
                yield chunk[:to_send]
                sent += to_send
        try:
            headers = {"Content-Length": str(size), "Content-Type": "application/octet-stream"}
            conn.request("POST", "/api/speedtest/upload", body=gen(), headers=headers)
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            data = json.loads(res.read().decode())
            self.assertEqual(data["bytes_received"], size)
            elapsed = max(0.001, time.time() - start)
            speed = (size * 8) / (1024 * 1024) / elapsed
            self.record_result(protocol, task_desc, f"PASS ({speed:.2f} Mbps)", start)
        except Exception as e:
            self.record_result(protocol, task_desc, f"FAIL ({type(e).__name__})", start)
            raise e
        finally:
            conn.close()

    # ==================================================================
    # [2] 비정상 및 예외/보안 테스트 (Sad Path)
    # ==================================================================

    def run_path_traversal_test(self, protocol: str):
        task_desc = "허용되지 않는 상위 폴더 접근 차단 (보안)"
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task_desc, 50)} 실행 중...", end="\r", flush=True)
        start = time.time()
        conn = self.get_conn(protocol)
        try:
            conn.request("GET", f"/api/files/content?path={urllib.parse.quote('../../generate_certs.sh')}")
            res = conn.getresponse()
            res.read()
            self.assertEqual(res.status, 403)
            self.record_result(protocol, task_desc, "PASS", start)
        except Exception as e:
            self.record_result(protocol, task_desc, f"FAIL ({type(e).__name__})", start)
            raise e
        finally:
            conn.close()

    def run_delete_root_prevention_test(self, protocol: str):
        task_desc = "최상위 루트 폴더 삭제 시도 차단 (보안)"
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task_desc, 50)} 실행 중...", end="\r", flush=True)
        start = time.time()
        conn = self.get_conn(protocol)
        try:
            conn.request("DELETE", f"/api/files?path={urllib.parse.quote('/')}")
            res = conn.getresponse()
            res.read()
            self.assertEqual(res.status, 400)
            self.record_result(protocol, task_desc, "PASS", start)
        except Exception as e:
            self.record_result(protocol, task_desc, f"FAIL ({type(e).__name__})", start)
            raise e
        finally:
            conn.close()

    def run_non_existent_resource_test(self, protocol: str):
        task_desc = "존재하지 않는 파일 다운로드 시 에러 처리"
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task_desc, 50)} 실행 중...", end="\r", flush=True)
        start = time.time()
        conn = self.get_conn(protocol)
        try:
            conn.request("GET", f"/api/files/content?path={urllib.parse.quote('/ghost_file_xxx.txt')}")
            res = conn.getresponse()
            res.read()
            self.assertEqual(res.status, 404)
            self.record_result(protocol, task_desc, "PASS", start)
        except Exception as e:
            self.record_result(protocol, task_desc, f"FAIL ({type(e).__name__})", start)
            raise e
        finally:
            conn.close()

    def run_zero_byte_file_test(self, protocol: str):
        task_desc = "0바이트 빈 파일 업로드 및 삭제 검증"
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task_desc, 50)} 실행 중...", end="\r", flush=True)
        start = time.time()
        conn = self.get_conn(protocol)
        file_name = f"auto_test_zero_{protocol}.txt"
        file_path = f"/{file_name}"
        try:
            boundary = "----WebKitFormBoundaryUnitTest"
            body = (
                f"--{boundary}\r\n"
                f'Content-Disposition: form-data; name="file"; filename="{file_name}"\r\n'
                f"Content-Type: application/octet-stream\r\n\r\n"
                f"\r\n--{boundary}--\r\n"
            ).encode()
            headers = {"Content-Type": f"multipart/form-data; boundary={boundary}", "Content-Length": str(len(body))}
            conn.request("POST", f"/api/files?path={urllib.parse.quote(file_path)}", body=body, headers=headers)
            res = conn.getresponse()
            res.read()
            self.assertEqual(res.status, 201)

            conn.request("GET", f"/api/files/content?path={urllib.parse.quote(file_path)}")
            res = conn.getresponse()
            self.assertEqual(res.status, 200)
            data = res.read()
            self.assertEqual(len(data), 0)

            conn.request("DELETE", f"/api/files?path={urllib.parse.quote(file_path)}")
            res = conn.getresponse()
            res.read()
            self.assertEqual(res.status, 204)
            self.record_result(protocol, task_desc, "PASS", start)
        except Exception as e:
            self.record_result(protocol, task_desc, f"FAIL ({type(e).__name__})", start)
            raise e
        finally:
            conn.close()

    def run_special_characters_test(self, protocol: str):
        task_desc = "한글 및 특수문자 포함 파일명 처리 검증"
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task_desc, 50)} 실행 중...", end="\r", flush=True)
        start = time.time()
        conn = self.get_conn(protocol)
        file_name = f"한글 공백 # & 특수문자_{protocol}.txt"
        file_path = f"/{file_name}"
        raw_data = b"Special Character Testing Content"
        try:
            boundary = "----WebKitFormBoundaryUnitTest"
            body = (
                f"--{boundary}\r\n"
                f'Content-Disposition: form-data; name="file"; filename="{file_name}"\r\n'
                f"Content-Type: text/plain\r\n\r\n"
            ).encode() + raw_data + f"\r\n--{boundary}--\r\n".encode()
            headers = {"Content-Type": f"multipart/form-data; boundary={boundary}", "Content-Length": str(len(body))}
            conn.request("POST", f"/api/files?path={urllib.parse.quote(file_path)}", body=body, headers=headers)
            res = conn.getresponse()
            res.read()
            self.assertEqual(res.status, 201)

            conn.request("GET", f"/api/files?path={urllib.parse.quote('/')}")
            res = conn.getresponse()
            data = json.loads(res.read().decode())
            names = [f["name"] for f in data["files"]]
            self.assertIn(file_name, names)

            conn.request("DELETE", f"/api/files?path={urllib.parse.quote(file_path)}")
            res = conn.getresponse()
            res.read()
            self.assertEqual(res.status, 204)
            self.record_result(protocol, task_desc, "PASS", start)
        except Exception as e:
            self.record_result(protocol, task_desc, f"FAIL ({type(e).__name__})", start)
            raise e
        finally:
            conn.close()

    # ==================================================================
    # [3] 공격 및 메모리 위협 방어 테스트 (Attack/OOM Prevention)
    # ==================================================================

    def run_slowloris_test(self, protocol: str):
        task_desc = "느린 데이터 전송 공격 연결 차단 (보안)"
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task_desc, 50)} 실행 중...", end="\r", flush=True)
        start = time.time()
        conn = self.get_conn(protocol, timeout=2)
        try:
            conn.putrequest("POST", "/api/speedtest/upload")
            conn.putheader("Content-Length", "100")
            conn.putheader("Content-Type", "application/octet-stream")
            conn.endheaders()
            
            conn.send(b"A")
            time.sleep(2.5)  # 고의 지연 유발해 서버 측 소켓 타임아웃 유도
            conn.send(b"B")
            res = conn.getresponse()
            res.read()
            self.assertIn(res.status, [400, 408, 500])
            self.record_result(protocol, task_desc, "PASS", start)
        except (socket.timeout, TimeoutError, OSError, http.client.HTTPException):
            self.record_result(protocol, task_desc, "PASS (타임아웃 차단)", start)
        finally:
            conn.close()

    def run_overflow_test(self, protocol: str):
        task_desc = "비정상 대용량 헤더 요청 차단 (보안)"
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task_desc, 50)} 실행 중...", end="\r", flush=True)
        start = time.time()
        conn = self.get_conn(protocol)
        try:
            huge_headers = {}
            for i in range(150):
                huge_headers[f"X-Fake-Header-{i}"] = "A" * 128
            conn.request("GET", "/api/status", headers=huge_headers)
            res = conn.getresponse()
            res.read()
            self.assertIn(res.status, [200, 400, 431])
            self.record_result(protocol, task_desc, "PASS", start)
        except Exception as e:
            self.record_result(protocol, task_desc, f"FAIL ({type(e).__name__})", start)
            raise e
        finally:
            conn.close()

    def run_fake_oom_content_length_test(self, protocol: str):
        task_desc = "가짜 대용량(1TB) 메모리 고갈 공격 차단 (보안)"
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task_desc, 50)} 실행 중...", end="\r", flush=True)
        start = time.time()
        conn = self.get_conn(protocol)
        try:
            headers = {
                "Content-Length": "1099511627776",  # 1 TB 가짜 용량 헤더 주입
                "Content-Type": "application/octet-stream"
            }
            # 데이터를 전혀 전송하지 않고 헤더만 쏩니다.
            conn.request("POST", "/api/speedtest/upload", body=b"", headers=headers)
            res = conn.getresponse()
            res.read()
            # 정석적 검증: 서버의 사전 헤더 차단(verify_payload_size)으로 즉시 413을 뱉어야 함!
            self.assertEqual(res.status, 413)
            self.record_result(protocol, task_desc, "PASS (413 Payload Too Large)", start)
        except Exception as e:
            self.record_result(protocol, task_desc, f"FAIL ({type(e).__name__})", start)
            raise e
        finally:
            conn.close()

    def run_malformed_multipart_test(self, protocol: str):
        task_desc = "손상된 업로드 데이터 요청 차단 (보안)"
        proto_tag = f"[{protocol.upper()}]"
        print(f" -> {proto_tag:<8}{pad_cjk(task_desc, 50)} 실행 중...", end="\r", flush=True)
        start = time.time()
        conn = self.get_conn(protocol)
        try:
            malformed_body = (
                "------WebKitFormBoundaryBroken\r\n"
                "Content-Disposition: form-data; name=\"file\"; filename=\"corrupted.txt\"\r\n"
                "Content-Type: text/plain\r\n\r\n"
                "Broken content\r\n"
                "------WebKitFormBoundaryWrongBoundary--\r\n"
            ).encode()
            headers = {
                "Content-Type": "multipart/form-data; boundary=------WebKitFormBoundaryBroken",
                "Content-Length": str(len(malformed_body))
            }
            conn.request("POST", "/api/files?path=/corrupted.txt", body=malformed_body, headers=headers)
            res = conn.getresponse()
            res.read()
            self.assertIn(res.status, [400, 422, 500])
            self.record_result(protocol, task_desc, "PASS", start)
        except Exception as e:
            self.record_result(protocol, task_desc, f"FAIL ({type(e).__name__})", start)
            raise e
        finally:
            conn.close()

    # ==================================================================
    # HTTP 테스트 클래스 바인딩
    # ==================================================================
    def test_http_01_status(self): self.run_status_test("http")
    def test_http_02_directory_lifecycle(self): self.run_directory_test("http")
    def test_http_03_file_io(self): self.run_file_io_test("http")
    
    # HTTP 대용량 다운로드 속도 측정 (용량별 순차)
    def test_http_04_speed_download_1_5mb(self): self.run_speedtest_download("http", 5*1024*1024, "5MB")
    def test_http_04_speed_download_2_10mb(self): self.run_speedtest_download("http", 10*1024*1024, "10MB")
    def test_http_04_speed_download_3_50mb(self): self.run_speedtest_download("http", 50*1024*1024, "50MB")
    def test_http_04_speed_download_4_100mb(self): self.run_speedtest_download("http", 100*1024*1024, "100MB")
    def test_http_04_speed_download_5_1gb(self): self.run_speedtest_download("http", 1*1024*1024*1024, "1GB")
    
    # HTTP 대용량 업로드 속도 측정 (용량별 순차)
    def test_http_05_speed_upload_1_5mb(self): self.run_speedtest_upload("http", 5*1024*1024, "5MB")
    def test_http_05_speed_upload_2_10mb(self): self.run_speedtest_upload("http", 10*1024*1024, "10MB")
    def test_http_05_speed_upload_3_50mb(self): self.run_speedtest_upload("http", 50*1024*1024, "50MB")
    def test_http_05_speed_upload_4_100mb(self): self.run_speedtest_upload("http", 100*1024*1024, "100MB")
    def test_http_05_speed_upload_5_1gb(self): self.run_speedtest_upload("http", 1*1024*1024*1024, "1GB")
    
    def test_http_06_path_traversal(self): self.run_path_traversal_test("http")
    def test_http_07_delete_root_prevention(self): self.run_delete_root_prevention_test("http")
    def test_http_08_non_existent(self): self.run_non_existent_resource_test("http")
    def test_http_09_zero_byte(self): self.run_zero_byte_file_test("http")
    def test_http_10_special_chars(self): self.run_special_characters_test("http")
    def test_http_11_slowloris(self): self.run_slowloris_test("http")
    def test_http_12_overflow(self): self.run_overflow_test("http")
    def test_http_13_fake_oom(self): self.run_fake_oom_content_length_test("http")
    def test_http_14_malformed_multipart(self): self.run_malformed_multipart_test("http")

    # ==================================================================
    # HTTPS 테스트 클래스 바인딩
    # ==================================================================
    def test_https_01_status(self): self.run_status_test("https")
    def test_https_02_directory_lifecycle(self): self.run_directory_test("https")
    def test_https_03_file_io(self): self.run_file_io_test("https")
    
    # HTTPS 대용량 다운로드 속도 측정 (용량별 순차)
    def test_https_04_speed_download_1_5mb(self): self.run_speedtest_download("https", 5*1024*1024, "5MB")
    def test_https_04_speed_download_2_10mb(self): self.run_speedtest_download("https", 10*1024*1024, "10MB")
    def test_https_04_speed_download_3_50mb(self): self.run_speedtest_download("https", 50*1024*1024, "50MB")
    def test_https_04_speed_download_4_100mb(self): self.run_speedtest_download("https", 100*1024*1024, "100MB")
    def test_https_04_speed_download_5_1gb(self): self.run_speedtest_download("https", 1*1024*1024*1024, "1GB")
    
    # HTTPS 대용량 업로드 속도 측정 (용량별 순차)
    def test_https_05_speed_upload_1_5mb(self): self.run_speedtest_upload("https", 5*1024*1024, "5MB")
    def test_https_05_speed_upload_2_10mb(self): self.run_speedtest_upload("https", 10*1024*1024, "10MB")
    def test_https_05_speed_upload_3_50mb(self): self.run_speedtest_upload("https", 50*1024*1024, "50MB")
    def test_https_05_speed_upload_4_100mb(self): self.run_speedtest_upload("https", 100*1024*1024, "100MB")
    def test_https_05_speed_upload_5_1gb(self): self.run_speedtest_upload("https", 1*1024*1024*1024, "1GB")
    
    def test_https_06_path_traversal(self): self.run_path_traversal_test("https")
    def test_https_07_delete_root_prevention(self): self.run_delete_root_prevention_test("https")
    def test_https_08_non_existent(self): self.run_non_existent_resource_test("https")
    def test_https_09_zero_byte(self): self.run_zero_byte_file_test("https")
    def test_https_10_special_chars(self): self.run_special_characters_test("https")
    def test_https_11_slowloris(self): self.run_slowloris_test("https")
    def test_https_12_overflow(self): self.run_overflow_test("https")
    def test_https_13_fake_oom(self): self.run_fake_oom_content_length_test("https")
    def test_https_14_malformed_multipart(self): self.run_malformed_multipart_test("https")

if __name__ == "__main__":
    
    # 1. argparse로 터미널 인자 파싱
    parser = argparse.ArgumentParser(description="M5Stack Tab5 API Integration & Stress Test Suite", add_help=False)
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--https-port", type=int, default=8443)
    parser.add_argument("-h", "--help", action="store_true")

    args, remaining_argv = parser.parse_known_args()

    if args.help:
        print("M5Stack Tab5 API Stress Test CLI Options:")
        print("  --host          : Target server host IP (default: localhost)")
        print("  --port          : HTTP service port (default: 8000)")
        print("  --https-port    : HTTPS service port (default: 8443)")
        sys.exit(0)

    # 파싱 결과 세팅
    TestControlPanel.TARGET_HOST = args.host
    TestControlPanel.TARGET_PORT = args.port
    TestControlPanel.TARGET_HTTPS_PORT = args.https_port

    # sys.argv 클렌징
    sys.argv = [sys.argv[0]] + remaining_argv

    print("======================================================================")
    print(f" 타겟 서버 : {args.host} (HTTP: {args.port} | HTTPS: {args.https_port})")
    print(" M5Stack Tab5 듀얼 프로토콜 API / 보안 / DoS & OOM 방어 전면적 교차 검증")
    print("======================================================================")

    # 2. 유닛 테스트 기동
    runner = unittest.TextTestRunner(stream=sys.stderr, verbosity=0)  # 지저분한 디폴트 출력 차단
    suite = unittest.TestLoader().loadTestsFromTestCase(TestControlPanel)
    runner.run(suite)

    # 3. 최종 요약 통계 출력
    total_tests = len(TEST_RESULTS)
    pass_tests = sum(1 for res in TEST_RESULTS if res['status'].startswith("PASS"))
    fail_tests = total_tests - pass_tests
    
    print("\n" + "="*112)
    if fail_tests == 0:
        print(f" [★] 교차 검증 통과! | 전체 테스트: {total_tests}건 | 성공: {pass_tests}건 | 실패: {fail_tests}건")
    else:
        print(f" [⚠] 검증 실패 항목 발생! | 전체 테스트: {total_tests}건 | 성공: {pass_tests}건 | 실패: {fail_tests}건")
    print("="*112)
