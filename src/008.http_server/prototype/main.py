import os
import sys
import subprocess
import shutil
import asyncio
from pathlib import Path
from typing import Optional

from fastapi import FastAPI, HTTPException, Request, Response
from fastapi.responses import StreamingResponse
from fastapi.staticfiles import StaticFiles
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

# 1. SSL 인증서 감시 및 생성 연동
BASE_DIR = Path(__file__).resolve().parent
KEY_PATH = BASE_DIR / "key.pem"
CERT_PATH = BASE_DIR / "cert.pem"
GEN_CERTS_SH = BASE_DIR / "generate_certs.sh"

def check_and_generate_certs():
    if not KEY_PATH.exists() or not CERT_PATH.exists():
        print("SSL 인증서 또는 개인키가 없습니다. generate_certs.sh를 실행하여 생성합니다...")
        if not GEN_CERTS_SH.exists():
            raise FileNotFoundError(f"인증서 생성 스크립트가 존재하지 않습니다: {GEN_CERTS_SH}")
        
        try:
            subprocess.run([str(GEN_CERTS_SH)], check=True, cwd=str(BASE_DIR))
            print("SSL 인증서 및 개인키 생성에 성공했습니다.")
        except subprocess.CalledProcessError as e:
            print(f"SSL 인증서 생성 스크립트 실행 실패 (반환 코드: {e.returncode}). 서버 기동을 차단합니다.", file=sys.stderr)
            raise e

check_and_generate_certs()

app = FastAPI(title="M5Stack Tab5 Simulator Server")

# 외부 단말 및 로컬 CORS 허용
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# 2. mock_sd 디렉토리 검증 및 Path Traversal 방어 헬퍼
MOCK_SD_DIR = (BASE_DIR / "mock_sd").resolve()
if not MOCK_SD_DIR.exists():
    MOCK_SD_DIR.mkdir(parents=True, exist_ok=True)

def get_safe_path(requested_path_str: str) -> Path:
    clean_path_str = requested_path_str.lstrip("/")
    target_path = (MOCK_SD_DIR / clean_path_str).resolve()
    
    # Path Traversal 방어 검증
    try:
        common = os.path.commonpath([str(MOCK_SD_DIR), str(target_path)])
        if common != str(MOCK_SD_DIR):
            raise HTTPException(status_code=403, detail="Access denied (Path Traversal block)")
    except ValueError:
        raise HTTPException(status_code=403, detail="Invalid path structure")
        
    return target_path

# 허용 최대 업로드 용량 정의 (100 MB)
MAX_UPLOAD_SIZE = 100 * 1024 * 1024

@app.middleware("http")
async def verify_payload_size_middleware(request: Request, call_next):
    """
    HTTP 헤더 단계에서 Content-Length를 사전 검증하여
    비정상 대용량 요청 발생 시 즉시 413 Payload Too Large로 거절 처리.
    """
    if request.method == "POST":
        content_length_str = request.headers.get("content-length")
        if content_length_str:
            try:
                content_length = int(content_length_str)
                # 속도 테스트 업로드인 경우 1.5GB, 일반 파일 업로드는 100MB 제한
                if request.url.path == "/api/speedtest/upload":
                    max_size = 1500 * 1024 * 1024
                else:
                    max_size = 100 * 1024 * 1024
                
                if content_length > max_size:
                    return Response(content="Payload Too Large", status_code=413)
            except ValueError:
                return Response(content="Invalid Content-Length header", status_code=400)
    return await call_next(request)

# DTO for directory creation
class CreateFolderRequest(BaseModel):
    path: str

# 3. API 엔드포인트 구현

@app.get("/api/status")
async def get_status():
    return {"status": "ok"}

@app.get("/api/files")
async def get_files(path: str = "/"):
    target_dir = get_safe_path(path)
    if not target_dir.exists() or not target_dir.is_dir():
        raise HTTPException(status_code=404, detail="Directory not found")
        
    files_list = []
    
    # Root 디렉토리가 아니라면 부모 디렉토리 ".." 추가
    if target_dir != MOCK_SD_DIR:
        files_list.append({
            "name": "..",
            "is_dir": True,
            "size": 0
        })
        
    try:
        for entry in os.scandir(target_dir):
            files_list.append({
                "name": entry.name,
                "is_dir": entry.is_dir(),
                "size": entry.stat().st_size if entry.is_file() else 0
            })
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to scan directory: {str(e)}")
        
    return {"files": files_list}

@app.post("/api/files")
async def create_resource(
    request: Request,
    path: Optional[str] = None
):
    # 용량 사전 검증은 미들웨어(verify_payload_size_middleware)에서 처리됨
    
    content_type = request.headers.get("content-type", "")
    
    # 1) 디렉토리 생성 (JSON 바디가 들어왔을 때)
    if "application/json" in content_type:
        try:
            body = await request.json()
            folder_path_str = body.get("path")
            if not folder_path_str:
                raise HTTPException(status_code=400, detail="Missing 'path' in body")
            target_path = get_safe_path(folder_path_str)
            target_path.mkdir(parents=True, exist_ok=True)
            return Response(status_code=201, headers={"Location": folder_path_str})
        except Exception as e:
            if isinstance(e, HTTPException):
                raise e
            raise HTTPException(status_code=500, detail=f"Failed to create directory: {str(e)}")
            
    # 2) 파일 업로드 (raw binary streaming)
    else:
        if not path:
            raise HTTPException(status_code=400, detail="Missing query path parameter")
            
        target_file_path = get_safe_path(path)
        
        # 32KB 고정 버퍼 크기만큼 스트림 수신하여 로컬 파일에 직접 기록
        try:
            target_file_path.parent.mkdir(parents=True, exist_ok=True)
            with open(target_file_path, "wb") as f:
                async for chunk in request.stream():
                    if chunk:
                        f.write(chunk)
            return Response(status_code=201)
        except Exception as e:
            raise HTTPException(status_code=500, detail=f"Failed to upload file: {str(e)}")

@app.delete("/api/files")
async def delete_resource(path: str):
    target_path = get_safe_path(path)
    if not target_path.exists():
        raise HTTPException(status_code=404, detail="Resource not found")
        
    # mock_sd 루트 디렉토리는 삭제하지 못하도록 차단
    if target_path == MOCK_SD_DIR:
        raise HTTPException(status_code=400, detail="Cannot delete root directory")
        
    try:
        if target_path.is_dir():
            shutil.rmtree(target_path)
        else:
            os.remove(target_path)
        return Response(status_code=204)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to delete resource: {str(e)}")

@app.get("/api/files/content")
async def get_file_content(path: str):
    target_path = get_safe_path(path)
    if not target_path.exists() or not target_path.is_file():
        raise HTTPException(status_code=404, detail="File not found")
        
    # 32KB 단위 스트리밍 응답 (O(1) 메모리 소비)
    def file_iterator():
        with open(target_path, "rb") as f:
            while True:
                chunk = f.read(32768)
                if not chunk:
                    break
                yield chunk
                
    file_name = target_path.name
    headers = {
        "Content-Disposition": f'attachment; filename="{file_name}"'
    }
    return StreamingResponse(file_iterator(), media_type="application/octet-stream", headers=headers)

# 4. 대역폭 측정(Speed Test) API

@app.get("/api/speedtest/download")
async def speedtest_download(size: int = 10485760):
    chunk_size = 32768
    null_chunk = bytes(chunk_size)
    
    async def download_generator():
        bytes_sent = 0
        while bytes_sent < size:
            to_send = min(chunk_size, size - bytes_sent)
            if to_send == chunk_size:
                yield null_chunk
            else:
                yield bytes(to_send)
            bytes_sent += to_send
            await asyncio.sleep(0)
            
    headers = {
        "Content-Length": str(size),
        "Content-Disposition": "attachment; filename=speedtest.bin"
    }
    return StreamingResponse(download_generator(), media_type="application/octet-stream", headers=headers)

@app.post("/api/speedtest/upload")
async def speedtest_upload(request: Request):
    # 용량 사전 검증은 미들웨어(verify_payload_size_middleware)에서 처리됨
    
    bytes_received = 0
    try:
        async for chunk in request.stream():
            bytes_received += len(chunk)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Upload test failed: {str(e)}")
        
    return {"status": "ok", "bytes_received": bytes_received}

# 5. 프론트엔드 정적 파일 마운트
app.mount("/", StaticFiles(directory=str(BASE_DIR / "static"), html=True), name="static")

# 6. asyncio.gather를 통한 HTTP & HTTPS 듀얼 포트 구동부
import uvicorn

async def run_servers():
    http_config = uvicorn.Config(
        app=app,
        host="0.0.0.0",
        port=8000,
        log_level="info"
    )
    
    https_config = uvicorn.Config(
        app=app,
        host="0.0.0.0",
        port=8443,
        ssl_keyfile=str(KEY_PATH),
        ssl_certfile=str(CERT_PATH),
        log_level="info"
    )
    
    http_server = uvicorn.Server(http_config)
    https_server = uvicorn.Server(https_config)
    
    await asyncio.gather(
        http_server.serve(),
        https_server.serve()
    )

if __name__ == "__main__":
    try:
        asyncio.run(run_servers())
    except KeyboardInterrupt:
        print("\n서버가 사용자에 의해 중단되었습니다.")
    except Exception as e:
        print(f"서버 실행 중 오류 발생: {e}", file=sys.stderr)
        sys.exit(1)
