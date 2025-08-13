import urllib.request

url = "http://localhost/shutdown"

try:
    with urllib.request.urlopen(url) as response:
        status_code = response.getcode()
        print(f"状态码: {status_code}")
except urllib.error.URLError as e:
    print(f"请求失败: {e}")