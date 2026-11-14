#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
一键修改 GoatCounter 登录凭据（部署在哪就改哪）。

用法:
    python gc_update_credentials.py                      # 按 cfg 改 email/password
    python gc_update_credentials.py --with-vhost         # 连站点 vhost 一起改
    python gc_update_credentials.py --status             # 只查看当前状态，不改

工作方式:
    1. 读取同目录 gc_credentials.cfg（GC_EMAIL/GC_PASSWORD/GC_VHOST）
    2. 自动探测本机 GC 安装位置（Windows/WSL/生产 Linux 通用）
    3. 调用 goatcounter db update user / update site 直接改库，即时生效，无需重启

跨平台路径探测（按顺序找第一个存在的）:
    二进制: ./goatcounter | /opt/goatcounter/goatcounter | ~/goatcounter/goatcounter
    数据库: ./goatcounter.db | /opt/goatcounter/goatcounter.db | ~/goatcounter/goatcounter.db
"""
import argparse
import os
import sqlite3
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CFG_NAME = "gc_credentials.cfg"


def find_gc():
    """探测本机 goatcounter 二进制和数据库，返回 (bin, db) 或抛异常。"""
    home = Path.home()
    candidates_bin = [
        HERE / "goatcounter",
        Path("/opt/goatcounter/goatcounter"),
        home / "goatcounter" / "goatcounter",
        HERE / "goatcounter.exe",
        Path("C:/opt/goatcounter/goatcounter.exe"),
    ]
    candidates_db = [
        HERE / "goatcounter.db",
        Path("/opt/goatcounter/goatcounter.db"),
        home / "goatcounter" / "goatcounter.db",
        Path("C:/opt/goatcounter/goatcounter.db"),
    ]
    gc_bin = next((p for p in candidates_bin if p.is_file()), None)
    gc_db = next((p for p in candidates_db if p.is_file()), None)
    if not gc_bin:
        raise SystemExit("错误: 找不到 goatcounter 二进制，尝试过:\n  " +
                         "\n  ".join(str(c) for c in candidates_bin))
    if not gc_db:
        raise SystemExit("错误: 找不到 goatcounter.db，尝试过:\n  " +
                         "\n  ".join(str(c) for c in candidates_db))
    return gc_bin, gc_db


def load_cfg():
    """读 gc_credentials.cfg，返回 dict。缺文件时用环境变量或提示。"""
    cfg_path = HERE / CFG_NAME
    vals = {}
    if cfg_path.is_file():
        for line in cfg_path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                k, v = line.split("=", 1)
                vals[k.strip()] = v.strip()
    else:
        # 环境变量兜底（CI/临时用）
        for k in ("GC_EMAIL", "GC_PASSWORD", "GC_VHOST"):
            if os.environ.get(k):
                vals[k] = os.environ[k]
    return vals, cfg_path


def run_gc(gc_bin, db, *args):
    cmd = [str(gc_bin), "db", f"-db=sqlite+file:{db}", *args]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"命令失败: {' '.join(cmd)}\n{r.stdout}\n{r.stderr}")
    return r.stdout


def db_query(db, sql, params=()):
    """只读查询 GC sqlite（绕过 goatcounter CLI 的 -find 精确匹配限制）。"""
    con = sqlite3.connect(f"file:{db}?mode=ro", uri=True)
    try:
        return con.execute(sql, params).fetchall()
    finally:
        con.close()


def main():
    ap = argparse.ArgumentParser(description="GoatCounter 凭据一键修改")
    ap.add_argument("--with-vhost", action="store_true",
                    help="连站点 vhost 一起更新（改 cname）")
    ap.add_argument("--status", action="store_true",
                    help="只查看当前用户/站点，不做任何修改")
    args = ap.parse_args()

    gc_bin, gc_db = find_gc()
    print(f"[0] 探测到 GC:  bin={gc_bin}\n             db={gc_db}")

    if args.status:
        sites = db_query(gc_db, "select site_id, cname, state from sites")
        users = db_query(gc_db, "select user_id, email from users")
        print("\n[current sites]")
        for row in sites:
            print(f"  site_id={row[0]}  cname={row[1]}  state={row[2]}")
        print("[current users]")
        for row in users:
            print(f"  user_id={row[0]}  email={row[1]}")
        if not sites and not users:
            print("  (库是空的，还没建站)")
        return

    cfg, cfg_path = load_cfg()
    email = cfg.get("GC_EMAIL")
    password = cfg.get("GC_PASSWORD")
    vhost = cfg.get("GC_VHOST")
    if not email or not password:
        raise SystemExit(
            f"错误: {cfg_path} 缺少 GC_EMAIL 或 GC_PASSWORD\n"
            f"（或设置环境变量 GC_EMAIL / GC_PASSWORD）")
    print(f"[0] cfg: {cfg_path}")

    print(f"[1/3] 更新用户凭据 -> {email}")
    # 从库里拿现有 email（CLI 的 -find 要求精确匹配，改过名后就找不到了）
    rows = db_query(gc_db, "select email from users order by user_id limit 1")
    if not rows:
        raise SystemExit("错误: 库里没有任何用户，请先建站")
    old = rows[0][0]
    if old == email:
        run_gc(gc_bin, gc_db, "update", "user", "-find", email,
               "-password", password)
    else:
        print(f"      (按现有用户 {old} 定位)")
        run_gc(gc_bin, gc_db, "update", "user", "-find", old,
               "-email", email, "-password", password)
    print("      password=已更新(bcrypt)，即时生效")

    if args.with_vhost:
        if not vhost:
            raise SystemExit("cfg 缺少 GC_VHOST，无法 --with-vhost")
        print(f"[2/3] 更新站点 vhost -> {vhost}")
        cur = db_query(gc_db, "select cname from sites order by site_id limit 1")
        if not cur:
            raise SystemExit("错误: 库里没有任何站点")
        old_vhost = cur[0][0]
        run_gc(gc_bin, gc_db, "update", "site", "-find", str(old_vhost),
               "-vhost", vhost)
    else:
        print("[2/3] 跳过 vhost（如需更新加 --with-vhost）")

    print("[3/3] 验证...")
    rows = db_query(gc_db, "select email from users where email = ?", (email,))
    if not rows:
        raise SystemExit("警告: 验证失败，按新 email 未查到用户")
    print(f"完成。登录凭据: {email} / (cfg 中的密码)")


if __name__ == "__main__":
    main()
