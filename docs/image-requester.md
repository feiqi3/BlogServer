# Image requests

`ImageRequester` uses an injected `FHttpClient` and `JobPool` to fetch image bytes and inspect JPEG, PNG, GIF, WebP, or BMP dimensions. Its callback runs on the client's loop.

```cpp
Blog::Utils::ImageRequester images(client, workers);
Blog::Utils::ImageRequest request;
request.url = "https://example.com/photo.jpg";
auto handle = images.requestImage(request, [](auto result) {
    if (!result.ok()) return; // inspect result.error
    const auto& image = *result.value;
    auto bytes = image.bytes;
    int width = image.info.width;
    int height = image.info.height;
});
// handle.cancel();
```

Use `requestDimensions` when only metadata is needed. It tries a bounded Range request and falls back to a bounded full download when needed. Defaults are a 12-second deadline, 8 MiB body limit, 64 KiB probe, 200 million pixels, and three redirects. HTTPS is verified; HTTPS-to-HTTP redirects and compressed responses are rejected. Metadata inspection does not validate every image pixel.

Keep the client, worker pool, and loop alive until callbacks run. The Server stops new refreshes, drains image work and the client, and then stops its shared loop. When a photo URL changes, dimensions are cleared and stale updates are rejected by URL.
