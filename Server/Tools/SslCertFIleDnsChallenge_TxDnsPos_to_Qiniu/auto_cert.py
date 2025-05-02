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

import tldextract

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
    optional_conf = {}
    if cfg.has_section('optional'):
        optional_conf = {
            'email' : cfg.get('optional','email',fallback='').strip,
        }
    return tencent, qiniu_conf,optional_conf


def get_dns_challenge(domain,email):

    cmd = [
        'certbot', 'certonly',
        '--manual',
        '--preferred-challenges', 'dns', '--agree-tos',
        '-d', domain
    ]
    if len(email) == 0:
        cmd.append('--register-unsafely-without-email')
    else:
        cmd.append('--email')
        cmd.append(email)

    proc = subprocess.Popen(cmd,
                            stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT,
                            text=True)
    name = value = None
    for line in proc.stdout:
        print(line, end='')
        if line.strip().startswith("(Y)es/(N)o:"):
            proc.stdin.write("Y\n")
        if line.strip().startswith("Please deploy a DNS TXT record under the name"):
            # 跳一行空行
            _ = next(proc.stdout)
            # 下一行包含记录名和提示
            nl = next(proc.stdout)
            name = nl.strip().split()[0]
            name = name.rstrip('.')
            print(nl)
            print("name = ",name)
            # 跳一行空行
            _ = next(proc.stdout)

            # 跳一行 with the following value:
            _ = next(proc.stdout)

            # 跳一行空行
            _ = next(proc.stdout)

            # 再下一行是 value
            vl = next(proc.stdout)
            print(vl)
            value = vl.strip()
            print("value = ",value)
        if line.strip().startswith("Before continuing, verify the TXT record has been deployed."):
            for line in iter(proc.stdout.readline, ''):
                # 这里只会在 readline() 返回空字符串（EOF）时停止
                print(line, end='')
                if line.strip().startswith("- - - - - - - - - - "):
                    print("Certbot operation hang up")
                    return name, value, proc
    return name, value, proc


def add_dns_record(tencent_conf, domain, sub_domain, txt_value):
    print(f"尝试为 {domain} 添加 DNS TXT 记录：{sub_domain} -> {txt_value}")
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

def create_domain_if_not_exists(tencent_conf, domain):
    """
    检查 DNSPod 是否已有该域名解析，如无则创建。
    """
    cred = credential.Credential(tencent_conf['secret_id'], tencent_conf['secret_key'])
    http_profile = HttpProfile()
    http_profile.endpoint = "dnspod.tencentcloudapi.com"
    client_profile = ClientProfile(httpProfile=http_profile)
    client = dnspod_client.DnspodClient(cred, "", client_profile)

    # 查询域名列表，使用 Keyword 参数过滤
    req = models.DescribeDomainListRequest()
    req.Keyword = domain
    req.Type = "ALL"
    req.Offset = 0
    req.Limit = 100
    resp = client.DescribeDomainList(req)
    if resp.DomainList:
        # 精确匹配域名
        for item in resp.DomainList:
            if item.Name == domain:
                print(f"域名 {domain} 已存在于 DNSPod。")
                return
    # 域名不存在，创建域名解析
    create_req = models.CreateDomainRequest()
    print(f"Query Domin: \"{domain}\"")
    create_req.Domain = domain.strip()
    create_resp = client.CreateDomain(create_req)
    print(f"已在 DNSPod 创建域名：{domain} (DomainInfo={create_resp.DomainInfo})")

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
        print("用法: python auto_cert.py <domain>")
        sys.exit(1)
    domain = sys.argv[1]

    tencent_conf, qiniu_conf,optional_conf = read_config()

    email = optional_conf.get('email','')
    ext = tldextract.extract(domain)
    main_domain = f"{ext.domain}.{ext.suffix}"
    # 1. 获取 DNS 验证记录
    name, value, proc = get_dns_challenge(domain,email)
    ext = tldextract.extract(name)
    # 2. 添加 DNS 记录
    create_domain_if_not_exists(tencent_conf,main_domain)
    record_id = add_dns_record(tencent_conf, main_domain, ext.subdomain, value)

    print("等待 DNS 生效（约60秒）...")
    time.sleep(60)

    # 3. 继续 Certbot 完成签发
    proc.stdin.write("\n")
    proc.stdin.flush()
    for line in proc.stdout:
        print(line)
    proc.wait()
    if proc.returncode != 0:
        print("Certbot 签发失败，请检查日志。")
        delete_dns_record(tencent_conf, main_domain, record_id)
        sys.exit(1)

    # 4. 上传证书到七牛云证书管理
    upload_cert_to_qiniu(qiniu_conf, domain)

    # 5. 删除 DNS 验证记录
    delete_dns_record(tencent_conf, main_domain, record_id)

    print("证书申请、上传及清理流程完成。")


if __name__ == "__main__":
    main()
