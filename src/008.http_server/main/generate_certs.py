#!/usr/bin/env python3
import os
import sys
import subprocess
import re

# 1. Check if cryptography is installed, install if missing
try:
    import cryptography
except ImportError:
    print("cryptography library not found. Installing...")
    try:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "cryptography"])
        import cryptography
        print("cryptography library successfully installed.")
    except Exception as e:
        print(f"Failed to install cryptography: {e}")
        sys.exit(1)

# Import required modules
from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives import serialization
import datetime
import ipaddress

def parse_predefine(header_path):
    """Parses predefine.h to extract configuration values."""
    config = {
        "CERT_PATH": "server.crt",
        "KEY_PATH": "server.key",
        "COUNTRY": "KR",
        "STATE": "Seoul",
        "LOCALITY": "Seoul",
        "ORGANIZATION": "ESP32",
        "HOST_NAME": "esp32.local"
    }
    
    if not os.path.exists(header_path):
        print(f"Warning: {header_path} not found. Using defaults.")
        return config

    print(f"Parsing configuration from {header_path}...")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Matches #define KEY [=] "VALUE"[;]
    def get_define_value(key):
        pattern = r'#define\s+' + key + r'\s*(?:=)?\s*"([^"]+)"\s*(?:;)?'
        match = re.search(pattern, content)
        return match.group(1) if match else None

    cert_path = get_define_value("HTTPS_CERT_PATH")
    key_path = get_define_value("HTTPS_KEY_PATH")
    country = get_define_value("HTTPS_COUNTRY_CODE")
    state = get_define_value("HTTPS_STATE")
    locality = get_define_value("HTTPS_LOCALITY")
    org = get_define_value("HTTPS_ORGANIZATION")
    hostname = get_define_value("HOST_NAME")

    if cert_path: config["CERT_PATH"] = cert_path
    if key_path: config["KEY_PATH"] = key_path
    if country: config["COUNTRY"] = country
    if state: config["STATE"] = state
    if locality: config["LOCALITY"] = locality
    if org: config["ORGANIZATION"] = org
    if hostname: config["HOST_NAME"] = hostname

    print(f"Parsed config: {config}")
    return config

def generate_certificates():
    # Target directory is the directory where this script is located
    main_dir = os.path.dirname(os.path.abspath(__file__))
    header_path = os.path.join(main_dir, "predefine.h")
    
    # Parse predefine.h
    config = parse_predefine(header_path)
    
    cert_path = os.path.join(main_dir, config["CERT_PATH"])
    key_path = os.path.join(main_dir, config["KEY_PATH"])
    
    # 2. Check if files already exist and are newer than predefine.h
    if os.path.exists(cert_path) and os.path.exists(key_path):
        header_mtime = os.path.getmtime(header_path) if os.path.exists(header_path) else 0
        cert_mtime = os.path.getmtime(cert_path)
        key_mtime = os.path.getmtime(key_path)
        
        if cert_mtime > header_mtime and key_mtime > header_mtime:
            print("Certificates are up-to-date. Skipping generation.")
            return
        else:
            print("predefine.h has been modified. Regenerating certificates...")

    print(f"Generating new self-signed certificate and private key...")
    
    # 3. Generate private key (RSA 2048)
    private_key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=2048,
    )
    
    # 4. Define certificate subject/issuer details using parsed configuration
    subject = issuer = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, config["COUNTRY"]),
        x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, config["STATE"]),
        x509.NameAttribute(NameOID.LOCALITY_NAME, config["LOCALITY"]),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, config["ORGANIZATION"]),
        x509.NameAttribute(NameOID.COMMON_NAME, config["HOST_NAME"]),
    ])
    
    # 5. Build certificate (valid for 10 years)
    cert = x509.CertificateBuilder().subject_name(
        subject
    ).issuer_name(
        issuer
    ).public_key(
        private_key.public_key()
    ).serial_number(
        x509.random_serial_number()
    ).not_valid_before(
        datetime.datetime.utcnow() - datetime.timedelta(days=1)
    ).not_valid_after(
        datetime.datetime.utcnow() + datetime.timedelta(days=3650)
    ).add_extension(
        x509.SubjectAlternativeName([
            x509.DNSName(config["HOST_NAME"]),
            x509.DNSName("localhost"),
            x509.IPAddress(ipaddress.IPv4Address("192.168.4.1")),
        ]),
        critical=False,
    ).sign(private_key, hashes.SHA256())
    
    # 6. Write private key
    with open(key_path, "wb") as f:
        f.write(private_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=serialization.NoEncryption()
        ))
        
    # 7. Write certificate
    with open(cert_path, "wb") as f:
        f.write(cert.public_bytes(serialization.Encoding.PEM))
        
    print(f"Successfully generated:\n  {cert_path}\n  {key_path}")

if __name__ == "__main__":
    generate_certificates()
