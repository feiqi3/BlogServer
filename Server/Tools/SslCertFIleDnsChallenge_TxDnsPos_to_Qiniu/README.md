setup your environment by following instructions   
```bash
pip install tencentcloud-sdk-python qiniu
sudo apt-get install certbot
```

create a file in the folder named `config.ini`
with following content:    

```
[tencentcloud]
secret_id = YOUR_TENCENTCLOUD_SECRET_ID
secret_key = YOUR_TENCENTCLOUD_SECRET_KEY

[qiniu]
access_key = YOUR_QINIU_ACCESS_KEY
secret_key = YOUR_QINIU_SECRET_KEY
bucket_name = YOUR_QINIU_ACCESS_KEY

```
