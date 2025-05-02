#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import subprocess
import configparser
import time
import os
import sys
from datetime import date

# 腾讯云 DNSPod SDK
from tencentcloud.common import credential
from tencentcloud.common.profile.http_profile import HttpProfile
from tencentcloud.common.profile.client_profile import ClientProfile
from tencentcloud.dnspod.v20210323 import dnspod_client, models
from tencentcloud.common.exception.tencent_cloud_sdk_exception import TencentCloudSDKException

# 七牛云 SDK & HTTP 请求
from qiniu import Auth
import requests


def read_config(path='config.ini'):
    cfg = configparser.ConfigParser()
    cfg.read(path)
    tencent = {
        'secret_id': cfg['tencentcloud']['secret_id'].strip(),
        'secret_key': cfg['tencentcloud']['secret_key'].strip()
    }
    qiniu_conf = {
        'access_key': cfg['qiniu']['access_key'].strip(),
        'secret_key': cfg['qiniu']['secret_key'].strip()
    }
    return tencent, qiniu_conf


def get_dns_challenge(domain):
    cmd = [
        'certbot', 'certonly',
        '--manual',
        '--preferred-challenges', 'dns',
        '-d', domain
    ]
    proc = subprocess.Popen(cmd,
                            stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            text=True)
    name = value = None
    for line in proc.stdout:
        print(line, end='')
        if line.strip().startswith("Please deploy a DNS TXT record under the name"):
            # 下一行包含记录名和提示
            nl = next(proc.stdout)
            name = nl.strip().split()[0]
            # 跳一行空行
            _ = next(proc.stdout)
            # 再下一行是 value
            vl = next(proc.stdout)
            value = vl.strip()
        if line.strip().startswith("Press Enter to Continue"):
            break
    return name, value, proc


def add_dns_record(tencent_conf, domain, sub_domain, txt_value):
    cred = credential.Credential(tencent_conf['secret_id'], tencent_conf['secret_key'])
    http_profile = HttpProfile()
    http_profile.endpoint = "dnspod.tencentcloudapi.com"
    client_profile = ClientProfile(httpProfile=http_profile)
    client = dnspod_client.DnspodClient(cred, "", client_profile)

    req = models.CreateRecordRequest()
    req.Domain = domain
    req.SubDomain = sub_domain
    req.RecordType = "TXT"
    req.RecordLine = "默认"
    req.Value = txt_value
    req.TTL = 600
    resp = client.CreateRecord(req)
    print(f"已添加 DNS TXT 记录：{sub_domain} -> {txt_value} (RecordId={resp.RecordId})")
    return resp.RecordId


def delete_dns_record(tencent_conf, domain, record_id):
    cred = credential.Credential(tencent_conf['secret_id'], tencent_conf['secret_key'])
    http_profile = HttpProfile()
    http_profile.endpoint = "dnspod.tencentcloudapi.com"
    client_profile = ClientProfile(httpProfile=http_profile)
    client = dnspod_client.DnspodClient(cred, "", client_profile)

    req = models.DeleteRecordRequest()
    req.Domain = domain
    req.RecordId = record_id
    client.DeleteRecord(req)
    print(f"已删除 DNS 记录：RecordId={record_id}")


def upload_cert_to_qiniu(qiniu_conf, domain):
    # 证书文件路径
    cert_dir = f"/etc/letsencrypt/live/{domain}"
    priv_path = os.path.join(cert_dir, 'privkey.pem')
    chain_path = os.path.join(cert_dir, 'fullchain.pem')
    if not os.path.isfile(priv_path) or not os.path.isfile(chain_path):
        raise FileNotFoundError("未找到证书或私钥文件，请确认 certbot 已成功签发证书。")

    # 读取私钥和证书链
    with open(priv_path, 'r') as f:
        pri = f.read()
    with open(chain_path, 'r') as f:
        ca = f.read()

    # 构建证书名称和 CommonName
    today = date.today().strftime('%Y%m%d')
    cert_name = f"{domain}-letsencrypt-{today}"

    # 七牛 API 地址和鉴权
    url = 'http://api.qiniu.com/sslcert'
    auth = Auth(qiniu_conf['access_key'], qiniu_conf['secret_key'])
    token = auth.token_of_request(url)

    # 请求数据
    data = {
        'name': cert_name,
        'common_name': domain,
        'pri': pri,
        'ca': ca
    }
    headers = {
        'Authorization': 'QBox ' + token,
        'Content-Type': 'application/json'
    }

    # 发送请求
    resp = requests.post(url, json=data, headers=headers)
    result = resp.json()
    if resp.status_code == 200 and 'certID' in result:
        print(f"证书已上传至七牛证书管理，certID={result['certID']}")
        return result['certID']
    else:
        raise RuntimeError(f"上传七牛证书失败: {result}")


def main():
    if len(sys.argv) != 2:
        print("用法: python auto_cert_v2.py <domain>")
        sys.exit(1)
    domain = sys.argv[1]

    tencent_conf, qiniu_conf = read_config()

    # 1. 获取 DNS 验证记录
    name, value, proc = get_dns_challenge(domain)
    # 2. 添加 DNS 记录
    record_id = add_dns_record(tencent_conf, domain, name, value)

    print("等待 DNS 生效（约60秒）...")
    time.sleep(60)

    # 3. 继续 Certbot 完成签发
    proc.stdin.write("\n")
    proc.stdin.flush()
    proc.wait()
    if proc.returncode != 0:
        print("Certbot 签发失败，请检查日志。")
        delete_dns_record(tencent_conf, domain, record_id)
        sys.exit(1)

    # 4. 上传证书到七牛云证书管理
    upload_cert_to_qiniu(qiniu_conf, domain)

    # 5. 删除 DNS 验证记录
    delete_dns_record(tencent_conf, domain, record_id)

    print("证书申请、上传及清理流程完成。")


if __name__ == "__main__":
    main()
