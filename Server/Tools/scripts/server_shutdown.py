import requests

url = "http://localhost/shutdown"

try:
    response = requests.get(url)
    print(f"关闭请求发送成功")
except requests.RequestException as e:
    print(f"关闭请求发送失败")
