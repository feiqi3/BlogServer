setup your environment by following instructions   
```bash
pip install tencentcloud-sdk-python qiniu tldextract
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

[optional]
email = YOUR_EMAIL@EMAIL
```    


Common problems:   
1. `certbot cannot import name 'appengine' from 'urllib3.contrib'`   
urllib3 was installed by both apt and pip. So remove pip's urllib3
---> pip3 uninstall urllib3    

2. ```The following error was encountered:
[Errno 13] Permission denied: '/var/log/letsencrypt'
Either run as root, or set --config-dir, --work-dir, and --logs-dir to writeable paths.```  
just run it with sudo  
