/* XAVATAR has no CDN dependencies. Uploaded images stay between this browser
   and the PS4; linked images are fetched by the browser from their host. */
"use strict";
(function () {
  var $ = function (id) {
    return document.getElementById(id);
  };
  var t = window.XaI18n.t;
  window.XaI18n.bind();
  var languageChoice = "auto";
  try { languageChoice = localStorage.getItem("xavatar.language") || "auto"; } catch (_) {}
  if (languageChoice !== "auto" && !Object.prototype.hasOwnProperty.call(window.XaI18n.languages, languageChoice)) languageChoice = "auto";
  var state = {
    mode: "both",
    connected: false,
    hostTest: false,
    systemLanguage: "en",
    users: [],
    avatars: [],
    crop: null,
    source: null,
    sourceUrl: "",
    previewUrl: "",
    filename: "",
    selected: "",
    busy: false,
    stopped: false,
    view: "create",
    zoom: 1,
    x: 0,
    y: 0,
  };
  var focusBeforeModal = null;
  var cropSnapshot = null;
  var activeModal = null;
  var connectionInFlight = false;
  var pollTimer = null;
  var userLoadPromise = null;
  var libraryLoadPromise = null;
  var canvas = $("cropCanvas");
  var context = canvas.getContext("2d");

  function toast(text, error) {
    var item = document.createElement("div");
    item.className = "toast" + (error ? " error" : "");
    var label = document.createElement("span");
    label.textContent = t(text);
    var close = document.createElement("button");
    close.textContent = "×";
    close.setAttribute("aria-label", t("Dismiss notification"));
    close.onclick = function () {
      item.remove();
    };
    item.appendChild(label);
    item.appendChild(close);
    $("toasts").appendChild(item);
    if (!error)
      setTimeout(function () {
        item.remove();
      }, 9000);
  }

  async function api(path, options) {
    options = options || {};
    if (options.method === "POST")
      options.headers = Object.assign({}, options.headers, {
        "X-XAVATAR": "1",
      });
    var controller =
      typeof AbortController !== "undefined" ? new AbortController() : null;
    if (controller) options.signal = controller.signal;
    var timer = controller
      ? setTimeout(function () {
          controller.abort();
        }, 20000)
      : null;
    try {
      var response = await fetch(path, options);
      var result = await response.json();
      if (!response.ok)
        throw new Error(
          result.message || t("The payload could not complete this operation."),
        );
      return result;
    } catch (error) {
      if (error.name === "AbortError")
        throw new Error(
          t("The payload took too long to respond. Check your connection."),
        );
      throw error;
    } finally {
      if (timer) clearTimeout(timer);
    }
  }

  function busy(text, detail) {
    state.busy = !!text;
    $("busyOverlay").hidden = !text;
    $("busyText").textContent = t(text || "");
    $("busyDetail").textContent = t(detail || "Keep this page open.");
    $("uploadProgress").hidden = true;
    document.body.setAttribute("aria-busy", text ? "true" : "false");
    updateAction();
  }

  function setConnection(online, test) {
    state.connected = online;
    state.hostTest = !!test;
    $("connection").classList.toggle("online", online);
    $("connectionText").textContent = online
      ? test
        ? t("Host test connected")
        : t("Payload connected")
      : state.stopped
        ? t("Service stopped")
        : t("Payload offline");
    $("connectionBanner").hidden = online;
    $("connectionMessage").textContent = state.stopped
      ? t("XAVATAR is stopped. Load the payload again on your PS4 to reopen port 4002.")
      : t("Cannot reach the payload. Check that XAVATAR is running on your PS4.");
    $("reconnect").textContent = state.stopped ? t("Check again") : t("Reconnect");
    $("footerStatus").textContent = online
      ? test
        ? t("LOCAL TEST · PORT 4002")
        : t("CONNECTED · PORT 4002")
      : state.stopped
        ? t("STOPPED · PORT 4002 CLOSED")
        : t("PAYLOAD OFFLINE");
    updateAction();
  }

  async function connect() {
    if (connectionInFlight || state.busy || state.stopped) return;
    connectionInFlight = true;
    updateAction();
    try {
      var status = await api("/api/status");
      if (state.stopped) return;
      if (status.name !== "XAVATAR") throw new Error("Unexpected service.");
      if (state.systemLanguage !== status.language) {
        state.systemLanguage = status.language || "en";
        if (languageChoice === "auto") applyLanguage();
      }
      var wasConnected = state.connected;
      setConnection(true, status.hostTest);
      if (!wasConnected) await Promise.all([loadUsers(), loadLibrary()]);
    } catch (_) {
      setConnection(false);
    } finally {
      connectionInFlight = false;
      updateAction();
    }
  }

  function populateUsers(select, previous) {
    select.textContent = "";
    var placeholder = document.createElement("option");
    placeholder.value = "";
    placeholder.textContent = state.users.length
      ? t("Select a PS4 user")
      : t("No PS4 users available");
    select.appendChild(placeholder);
    state.users.forEach(function (user) {
      var option = document.createElement("option");
      option.value = user.id;
      option.textContent = user.name;
      select.appendChild(option);
    });
    if (
      state.users.some(function (u) {
        return u.id === previous;
      })
    )
      select.value = previous;
  }

  function loadUsers() {
    if (state.stopped) return Promise.resolve();
    if (userLoadPromise) return userLoadPromise;
    $("userHelp").textContent = t("Loading usernames…");
    $("refreshUsers").disabled = true;
    userLoadPromise = (async function () {
      try {
        var result = await api("/api/users");
        state.users = result.users;
        populateUsers($("userSelect"), $("userSelect").value);
        populateUsers($("installUser"), $("installUser").value);
        $("userHelp").textContent = state.users.length
          ? t("Select the profile for your new look.")
          : t("No local users found. Refresh after signing in on your PS4.");
      } catch (error) {
        state.users = [];
        populateUsers($("userSelect"));
        populateUsers($("installUser"));
        $("userHelp").textContent =
          t("Could not load usernames. Use Refresh to try again.");
        toast(error.message, true);
      } finally {
        $("refreshUsers").disabled = false;
        userLoadPromise = null;
        updateAction();
      }
    })();
    return userLoadPromise;
  }

  function loadLibrary() {
    if (state.stopped) return Promise.resolve();
    if (libraryLoadPromise) return libraryLoadPromise;
    $("librarySummary").textContent = t("Loading avatars…");
    $("refreshLibrary").disabled = true;
    $("libraryError").hidden = true;
    libraryLoadPromise = (async function () {
      try {
        var result = await api("/api/avatars");
        state.avatars = result.avatars.sort(function (a, b) {
          return a.name.localeCompare(b.name);
        });
        $("libraryCount").textContent = state.avatars.length;
        renderLibrary();
        if (result.truncated)
          toast(
            "Showing the first 1,000 avatars. Move some packages out of /data/AVATARS to see the rest.",
            true,
          );
      } catch (error) {
        $("librarySummary").textContent = t("Library unavailable");
        $("libraryError").textContent =
          t(error.message);
        $("libraryError").hidden = false;
        $("libraryEmpty").hidden = true;
      } finally {
        $("refreshLibrary").disabled = false;
        libraryLoadPromise = null;
      }
    })();
    return libraryLoadPromise;
  }

  function renderLibrary() {
    var term = $("search").value.toLowerCase();
    var avatars = state.avatars.filter(function (a) {
      return a.name.toLowerCase().indexOf(term) !== -1;
    });
    $("libraryGrid").textContent = "";
    $("libraryEmpty").hidden = !!avatars.length;
    if (!avatars.length) {
      $("libraryEmpty").querySelector("h2").textContent = term
        ? t("No matching avatars.")
        : t("Your next look starts here.");
      $("libraryEmpty").querySelector("p").textContent = term
        ? t("Try another name or clear your search.")
        : t("Create and save your first avatar to start your collection.");
      $("emptyCreate").hidden = !!term;
    }
    $("librarySummary").textContent = t(term ? "{count} avatars found" : "{count} avatars in your collection", {count: avatars.length});
    avatars.forEach(function (avatar) {
      var card = document.createElement("article");
      card.className = "avatar-card";
      var thumb = document.createElement("div");
      thumb.className = "avatar-thumb";
      var img = document.createElement("img");
      img.alt = avatar.name;
      img.loading = "lazy";
      img.src = "/api/preview?name=" + encodeURIComponent(avatar.name);
      var badge = document.createElement("span");
      badge.className = "pill";
      badge.textContent = "XAVATAR";
      var info = document.createElement("div");
      info.className = "avatar-info";
      var name = document.createElement("strong");
      name.textContent = avatar.name.replace(/\.xavatar$/, "");
      name.title = avatar.name;
      var meta = document.createElement("small");
      meta.textContent =
        avatar.name + " · " + Math.ceil(avatar.size / 1024) + " KB";
      var button = document.createElement("button");
      button.className = "btn";
      button.textContent = t("Install avatar");
      button.disabled = !state.connected || state.busy;
      button.onclick = function () {
        openInstall(avatar.name);
      };
      img.onerror = function () {
        img.hidden = true;
        badge.textContent = t("INVALID PACKAGE");
        button.disabled = true;
        button.dataset.invalid = "true";
        meta.textContent = t("Preview unavailable · check this package");
      };
      thumb.appendChild(img);
      thumb.appendChild(badge);
      info.appendChild(name);
      info.appendChild(meta);
      info.appendChild(button);
      var actions = document.createElement("div");
      actions.className = "avatar-actions";
      var download = document.createElement("button");
      download.className = "btn small";
      download.textContent = t("Download");
      download.disabled = !state.connected || state.busy;
      download.onclick = function () { downloadAvatar(avatar.name); };
      var remove = document.createElement("button");
      remove.className = "btn small danger";
      remove.textContent = t("Delete");
      remove.disabled = !state.connected || state.busy;
      remove.onclick = function () {
        state.selected = avatar.name;
        $("deleteName").textContent = avatar.name;
        openModal("deleteModal");
      };
      actions.appendChild(download);
      actions.appendChild(remove);
      info.appendChild(actions);
      card.appendChild(thumb);
      card.appendChild(info);
      $("libraryGrid").appendChild(card);
    });
  }

  function view(name) {
    state.view = name;
    $("createView").hidden = name !== "create";
    $("libraryView").hidden = name !== "library";
    document.querySelectorAll("[data-view]").forEach(function (button) {
      var active = button.dataset.view === name;
      button.classList.toggle("active", active);
      if (active) button.setAttribute("aria-current", "page");
      else button.removeAttribute("aria-current");
    });
    if (name === "library") loadLibrary();
  }

  function mode(value) {
    state.mode = value;
    document.querySelectorAll("[data-mode]").forEach(function (button) {
      var active = button.dataset.mode === value;
      button.classList.toggle("selected", active);
      button.setAttribute("aria-pressed", String(active));
    });
    $("userField").hidden = value === "save";
    $("filenameField").hidden = value === "install";
    $("workflowHint").textContent =
      value === "both"
        ? t("INSTALL + SAVE")
        : value === "save"
          ? t("SAVE TO LIBRARY")
          : t("DIRECT INSTALL");
    $("createButton").querySelector("span").textContent =
      value === "both"
        ? t("Create & install")
        : value === "save"
          ? t("Create XAVATAR file")
          : t("Install without saving");
    updateAction();
  }

  function validName(input) {
    return /^[A-Za-z0-9_-][A-Za-z0-9 _-]{0,91}$/.test(
      (input || $("filename")).value.trim(),
    );
  }
  function updateAction() {
    var needUser = state.mode !== "save",
      needName = state.mode !== "install";
    var disabled =
      state.busy ||
      !state.connected ||
      !state.crop ||
      (needUser && !$("userSelect").value) ||
      (needName && !validName());
    $("createButton").disabled = !!disabled;
    $("stopService").disabled =
      !state.connected || state.busy || connectionInFlight || state.stopped;
    $("refreshUsers").disabled = state.stopped || state.busy;
    $("refreshLibrary").disabled = state.stopped || state.busy;
    $("languageSelect").disabled = state.busy;
    $("uploadAvatar").disabled = !state.connected || state.busy;
    $("uploadButton").disabled = !state.connected || state.busy || !$("packageInput").files.length || !validName($("packageName"));
    $("deleteButton").disabled = !state.connected || state.busy;
    $("libraryGrid").querySelectorAll("button").forEach(function (button) {
      button.disabled = !state.connected || state.busy || button.dataset.invalid === "true";
    });
    var name = state.users.find(function (u) {
      return u.id === $("userSelect").value;
    });
    $("previewName").textContent =
      name && needUser ? name.name : t("Your next identity");
    $("installButton").disabled =
      !state.connected || !$("installUser").value || state.busy;
    $("actionHint").textContent = !state.connected
      ? t("Connect to the payload to continue.")
      : !state.crop
        ? t("Choose an image to get started.")
        : needUser && !$("userSelect").value
          ? t("Select the PS4 user for this avatar.")
          : needName && !validName()
            ? t("Use letters, numbers, spaces, hyphens or underscores for the name.")
            : state.mode === "install"
              ? t("Applied directly. No .xavatar file will be saved.")
              : state.mode === "save"
                ? t("Your package will be ready in the avatar library.")
                : t("Apply it now. Keep it in your library.");
  }

  function openModal(id) {
    focusBeforeModal = document.activeElement;
    activeModal = $(id);
    activeModal.hidden = false;
    document.body.style.overflow = "hidden";
    var target = activeModal.querySelector("button,select,input");
    if (target) target.focus();
  }
  function closeModal(id) {
    if (state.busy) return;
    if (id === "cropModal" && cropSnapshot) {
      if (state.sourceUrl !== cropSnapshot.sourceUrl)
        URL.revokeObjectURL(state.sourceUrl);
      Object.assign(state, cropSnapshot);
      cropSnapshot = null;
    }
    $(id).hidden = true;
    activeModal = null;
    document.body.style.overflow = "";
    if (focusBeforeModal && focusBeforeModal.focus) focusBeforeModal.focus();
  }
  document.addEventListener("keydown", function (event) {
    if (event.key === "Escape" && activeModal && !state.busy)
      closeModal(activeModal.id);
    if (event.key !== "Tab" || !activeModal || state.busy) return;
    var focusable = Array.from(
      activeModal.querySelectorAll("button:not(:disabled),select,input"),
    ).filter(function (e) {
      return e.offsetParent !== null;
    });
    var first = focusable[0],
      last = focusable[focusable.length - 1];
    if (event.shiftKey && document.activeElement === first) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && document.activeElement === last) {
      event.preventDefault();
      first.focus();
    }
  });

  function cropGeometry() {
    var scale =
      Math.max(
        440 / state.source.naturalWidth,
        440 / state.source.naturalHeight,
      ) * state.zoom;
    var width = state.source.naturalWidth * scale,
      height = state.source.naturalHeight * scale;
    return {
      width: width,
      height: height,
      maxX: (width - 440) / 2,
      maxY: (height - 440) / 2,
    };
  }
  function drawCrop() {
    if (!state.source) return;
    var g = cropGeometry();
    context.clearRect(0, 0, 440, 440);
    context.imageSmoothingEnabled = true;
    context.imageSmoothingQuality = "high";
    context.drawImage(
      state.source,
      (440 - g.width) / 2 + (g.maxX * state.x) / 100,
      (440 - g.height) / 2 + (g.maxY * state.y) / 100,
      g.width,
      g.height,
    );
    $("zoom").value = state.zoom;
    $("zoomValue").textContent = state.zoom.toFixed(1) + "×";
    $("cropX").value = state.x;
    $("cropY").value = state.y;
    $("cropX").disabled = g.maxX < 0.1;
    $("cropY").disabled = g.maxY < 0.1;
  }
  function resetCrop() {
    state.zoom = 1;
    state.x = 0;
    state.y = 0;
    drawCrop();
  }
  function openCropFromImage(image, sourceUrl, filename) {
    if (image.naturalWidth * image.naturalHeight > 32000000) {
      if (sourceUrl) URL.revokeObjectURL(sourceUrl);
      busy("");
      toast(
        t("This image is too large. Choose an image below 32 megapixels."),
        true,
      );
      return false;
    }
    snapshotCrop();
    state.source = image;
    state.sourceUrl = sourceUrl;
    state.filename = filename;
    busy("");
    resetCrop();
    openModal("cropModal");
    return true;
  }
  function loadImage(file) {
    if (!file || state.busy) return;
    if (!/^image\/(png|jpeg|webp|gif)$/.test(file.type)) {
      toast(t("Choose a PNG, JPG, WebP or GIF image."), true);
      return;
    }
    if (file.size > 20 * 1024 * 1024) {
      toast(t("Choose an image smaller than 20 MB."), true);
      return;
    }
    busy(t("Loading your image…"), t("Preparing the crop editor."));
    var url = URL.createObjectURL(file),
      image = new Image();
    image.onload = function () {
      openCropFromImage(image, url, file.name);
    };
    image.onerror = function () {
      URL.revokeObjectURL(url);
      busy("");
      toast(t("This image could not be opened. Try another image."), true);
    };
    image.src = url;
  }
  function validImageUrl(value) {
    try {
      var url = new URL((value || "").trim());
      return url.protocol === "http:" || url.protocol === "https:" ? url : null;
    } catch (_) {
      return null;
    }
  }
  function linkedName(url) {
    var name = "";
    try {
      var path = url.pathname;
      name = decodeURIComponent(path.substring(path.lastIndexOf("/") + 1));
    } catch (_) {}
    return name || "linked-image";
  }
  function loadImageFromUrl(value) {
    if (state.busy) return;
    var url = validImageUrl(value);
    if (!url) {
      toast(
        t("Enter a valid image link starting with http:// or https://."),
        true,
      );
      return;
    }
    if (/\.gif$/i.test(url.pathname)) {
      toast(
        t("GIF links are not supported. Use a PNG, JPG or WebP link."),
        true,
      );
      return;
    }
    busy(t("Loading the linked image…"), t("Preparing the crop editor."));
    var image = new Image();
    image.crossOrigin = "anonymous";
    image.onload = function () {
      openCropFromImage(image, "", linkedName(url));
    };
    image.onerror = function () {
      loadImageThroughPayload(url);
    };
    image.src = url.href;
  }
  function linkFailure(serverMessage) {
    busy("");
    toast(
      serverMessage ||
        t(
          "That link could not be loaded. The site may block it. Download the image and choose the file instead.",
        ),
      true,
    );
  }
  function loadImageThroughPayload(url) {
    fetch("/api/fetch?url=" + encodeURIComponent(url.href))
      .then(function (response) {
        if (response.ok) return response.blob();
        return response.json().then(
          function (data) {
            throw new Error(data && data.message ? data.message : "");
          },
          function () {
            throw new Error("");
          },
        );
      })
      .then(function (blob) {
        var objectUrl = URL.createObjectURL(blob),
          image = new Image();
        image.onload = function () {
          openCropFromImage(image, objectUrl, linkedName(url));
        };
        image.onerror = function () {
          URL.revokeObjectURL(objectUrl);
          linkFailure("");
        };
        image.src = objectUrl;
      })
      .catch(function (failure) {
        linkFailure(failure && failure.message ? failure.message : "");
      });
  }

  var drag = null;
  function snapshotCrop() {
    cropSnapshot = {
      source: state.source,
      sourceUrl: state.sourceUrl,
      filename: state.filename,
      zoom: state.zoom,
      x: state.x,
      y: state.y,
    };
  }
  canvas.addEventListener("pointerdown", function (event) {
    if (!state.source) return;
    drag = {
      x: event.clientX,
      y: event.clientY,
      originalX: state.x,
      originalY: state.y,
    };
    canvas.setPointerCapture(event.pointerId);
  });
  canvas.addEventListener("pointermove", function (event) {
    if (!drag) return;
    var g = cropGeometry(),
      ratio = 440 / canvas.getBoundingClientRect().width;
    state.x = g.maxX
      ? Math.max(
          -100,
          Math.min(
            100,
            drag.originalX +
              (((event.clientX - drag.x) * ratio) / g.maxX) * 100,
          ),
        )
      : 0;
    state.y = g.maxY
      ? Math.max(
          -100,
          Math.min(
            100,
            drag.originalY +
              (((event.clientY - drag.y) * ratio) / g.maxY) * 100,
          ),
        )
      : 0;
    drawCrop();
  });
  canvas.addEventListener("pointerup", function () {
    drag = null;
  });
  canvas.addEventListener("pointercancel", function () {
    drag = null;
  });
  canvas.addEventListener(
    "wheel",
    function (event) {
      if (!state.source) return;
      event.preventDefault();
      state.zoom = Math.max(1, Math.min(4, state.zoom - event.deltaY * 0.002));
      drawCrop();
    },
    { passive: false },
  );
  $("zoom").oninput = function () {
    state.zoom = Number(this.value);
    drawCrop();
  };
  $("cropX").oninput = function () {
    state.x = Number(this.value);
    drawCrop();
  };
  $("cropY").oninput = function () {
    state.y = Number(this.value);
    drawCrop();
  };
  $("resetCrop").onclick = resetCrop;
  $("applyCrop").onclick = function () {
    busy(t("Processing your crop…"), t("Preparing a 440 × 440 image."));
    canvas.toBlob(function (blob) {
      if (!blob) {
        busy("");
        toast(t("Could not process the image. Please try again."), true);
        return;
      }
      state.crop = blob;
      if (cropSnapshot && cropSnapshot.sourceUrl !== state.sourceUrl)
        URL.revokeObjectURL(cropSnapshot.sourceUrl);
      cropSnapshot = null;
      if (state.previewUrl) URL.revokeObjectURL(state.previewUrl);
      state.previewUrl = URL.createObjectURL(blob);
      $("cropPreview").src = state.previewUrl;
      $("profilePreview").src = state.previewUrl;
      $("profilePreview").hidden = false;
      $("profilePlaceholder").hidden = true;
      $("dropzone").hidden = true;
      $("imageWorkspace").hidden = false;
      $("sourceName").textContent = state.filename;
      if (!$("filename").value)
        $("filename").value =
          state.filename
            .replace(/\.[^.]+$/, "")
            .replace(/[^A-Za-z0-9_-]+/g, "-")
            .replace(/^-+|-+$/g, "")
            .slice(0, 92) || "my-avatar";
      busy("");
      closeModal("cropModal");
      updateAction();
      toast(t("Crop ready. Your next look is taking shape."));
    }, "image/png");
  };

  function upload(path, blob, packageUpload) {
    return new Promise(function (resolve, reject) {
      var xhr = new XMLHttpRequest();
      xhr.open("POST", path);
      xhr.timeout = 120000;
      xhr.setRequestHeader("Content-Type", packageUpload ? "application/octet-stream" : "image/png");
      xhr.setRequestHeader("X-XAVATAR", "1");
      xhr.upload.onprogress = function (event) {
        if (event.lengthComputable) {
          $("uploadProgress").hidden = false;
          $("uploadProgress").value = (event.loaded / event.total) * 100;
          $("busyDetail").textContent =
            t("Sending your file… {percent}%", {percent: Math.round((event.loaded / event.total) * 100)});
        }
      };
      xhr.upload.onload = function () {
        $("uploadProgress").hidden = true;
        $("busyDetail").textContent =
          packageUpload ? t("Checking and saving your package…") : state.mode === "install"
            ? t("Creating textures and installing your avatar…")
            : state.mode === "save"
              ? t("Creating textures and saving your package…")
              : t("Creating textures, saving and installing your avatar…");
      };
      xhr.onload = function () {
        try {
          var result = JSON.parse(xhr.responseText);
          if (xhr.status < 200 || xhr.status >= 300)
            throw new Error(result.message || t("Operation failed."));
          resolve(result);
        } catch (error) {
          reject(error);
        }
      };
      xhr.onerror = function () {
        reject(
          new Error(
            t("Connection lost. Reconnect and check the library before retrying."),
          ),
        );
      };
      xhr.ontimeout = function () {
        reject(
          new Error(
            t("The operation timed out. Check the library and profile before retrying."),
          ),
        );
      };
      xhr.send(blob);
    });
  }

  $("createButton").onclick = async function () {
    if (this.disabled) return;
    var path = "/api/create?mode=" + state.mode;
    if (state.mode !== "save")
      path += "&user=" + encodeURIComponent($("userSelect").value);
    if (state.mode !== "install")
      path +=
        "&name=" + encodeURIComponent($("filename").value.trim() + ".xavatar");
    busy(
      state.mode === "save"
        ? t("Creating your XAVATAR file…")
        : state.mode === "install"
          ? t("Installing your new look…")
          : t("Creating & installing your avatar…"),
    );
    try {
      var result = await upload(path, state.crop);
      toast(result.message);
      await loadLibrary();
    } catch (error) {
      toast(error.message, true);
    } finally {
      busy("");
    }
  };

  async function openInstall(name) {
    state.selected = name;
    $("installName").textContent = name;
    $("installPreview").src = "/api/preview?name=" + encodeURIComponent(name);
    populateUsers($("installUser"));
    openModal("installModal");
    updateAction();
    await loadUsers();
  }
  $("installButton").onclick = async function () {
    if (this.disabled) return;
    busy(
      t("Installing your avatar…"),
      t("Applying your chosen look to the selected PS4 user."),
    );
    try {
      var result = await api(
        "/api/install?name=" +
          encodeURIComponent(state.selected) +
          "&user=" +
          encodeURIComponent($("installUser").value),
        { method: "POST" },
      );
      busy("");
      closeModal("installModal");
      toast(result.message);
    } catch (error) {
      busy("");
      toast(error.message, true);
    }
  };

  document.querySelectorAll("[data-view]").forEach(function (button) {
    button.onclick = function () {
      view(button.dataset.view);
    };
  });
  document.querySelectorAll("[data-mode]").forEach(function (button) {
    button.onclick = function () {
      mode(button.dataset.mode);
    };
  });
  document.querySelectorAll("[data-close]").forEach(function (button) {
    button.onclick = function () {
      closeModal(button.dataset.close);
    };
  });
  ["chooseImage", "replaceImage"].forEach(function (id) {
    $(id).onclick = function () {
      $("imageInput").click();
    };
  });
  $("imageInput").onchange = function () {
    loadImage(this.files[0]);
    this.value = "";
  };
  $("loadFromUrl").onclick = function () {
    loadImageFromUrl($("imageUrl").value);
  };
  $("imageUrl").onkeydown = function (event) {
    if (event.key === "Enter") {
      event.preventDefault();
      loadImageFromUrl(this.value);
    }
  };
  $("linkImage").onclick = function () {
    $("dropzone").hidden = false;
    $("imageWorkspace").hidden = true;
    $("imageUrl").focus();
  };
  $("editCrop").onclick = function () {
    snapshotCrop();
    drawCrop();
    openModal("cropModal");
  };
  $("filename").oninput = updateAction;
  $("userSelect").onchange = updateAction;
  $("installUser").onchange = updateAction;
  $("refreshUsers").onclick = loadUsers;
  $("refreshLibrary").onclick = loadLibrary;
  function reconnect() {
    state.stopped = false;
    if (!pollTimer) pollTimer = setInterval(connect, 15000);
    return connect();
  }
  $("reconnect").onclick = reconnect;
  $("connection").onclick = reconnect;
  $("stopService").onclick = async function () {
    if (this.disabled) return;
    busy(t("Stopping XAVATAR…"), t("Closing port 4002 and ending the payload."));
    try {
      await api("/api/stop", { method: "POST" });
      state.stopped = true;
      clearInterval(pollTimer);
      pollTimer = null;
      setConnection(false);
      toast(t("XAVATAR stopped. Load the payload again to reopen port 4002."));
    } catch (error) {
      toast(error.message, true);
    } finally {
      busy("");
    }
  };
  $("search").oninput = renderLibrary;
  ["newAvatar", "emptyCreate"].forEach(function (id) {
    $(id).onclick = function () {
      view("create");
    };
  });
  ["dragenter", "dragover"].forEach(function (event) {
    $("dropzone").addEventListener(event, function (e) {
      e.preventDefault();
      $("dropzone").classList.add("dragging");
    });
  });
  ["dragleave", "drop"].forEach(function (event) {
    $("dropzone").addEventListener(event, function (e) {
      e.preventDefault();
      $("dropzone").classList.remove("dragging");
    });
  });
  $("dropzone").addEventListener("drop", function (event) {
    loadImage(event.dataTransfer.files[0]);
  });
  window.addEventListener("beforeunload", function (event) {
    if (state.busy) {
      event.preventDefault();
      event.returnValue = "";
    }
  });
  function applyLanguage() {
    window.XaI18n.set(languageChoice === "auto" ? state.systemLanguage : languageChoice);
    $("languageSelect").options[0].textContent = t("Automatic (PS4)");
    $("languageSelect").value = languageChoice;
    populateUsers($("userSelect"), $("userSelect").value);
    populateUsers($("installUser"), $("installUser").value);
    $("userHelp").textContent = t(state.users.length ? "Select the profile for your new look." : "Select a user on your PS4.");
    setConnection(state.connected, state.hostTest);
    mode(state.mode);
    renderLibrary();
  }
  var autoOption = document.createElement("option");
  autoOption.value = "auto";
  $("languageSelect").appendChild(autoOption);
  Object.keys(window.XaI18n.languages).forEach(function (code) {
    var option = document.createElement("option");
    option.value = code;
    option.lang = code;
    option.textContent = window.XaI18n.languages[code];
    $("languageSelect").appendChild(option);
  });
  $("languageSelect").onchange = function () {
    languageChoice = this.value;
    try { localStorage.setItem("xavatar.language", languageChoice); } catch (_) {}
    applyLanguage();
  };
  $("uploadAvatar").onclick = function () {
    $("packageInput").value = "";
    $("packageName").value = "";
    $("packageSelected").textContent = "";
    openModal("uploadModal");
    updateAction();
  };
  $("choosePackage").onclick = function () { $("packageInput").click(); };
  $("packageInput").onchange = function () {
    var file = this.files[0];
    if (file && (!/\.xavatar$/i.test(file.name) || !file.size || file.size > 4 * 1024 * 1024)) {
      this.value = "";
      toast(t("Choose a .xavatar file smaller than 4 MB."), true);
    } else if (file) {
      $("packageName").value = file.name.replace(/\.xavatar$/i, "").replace(/[^A-Za-z0-9 _-]/g, "-").trim().slice(0, 92) || "my-avatar";
    }
    $("packageSelected").textContent = this.files[0] ? this.files[0].name : "";
    updateAction();
  };
  $("packageName").oninput = updateAction;
  $("uploadButton").onclick = async function () {
    if (this.disabled) return;
    busy(t("Uploading your avatar…"));
    try {
      var result = await upload("/api/upload?name=" + encodeURIComponent($("packageName").value.trim() + ".xavatar"), $("packageInput").files[0], true);
      busy("");
      closeModal("uploadModal");
      toast(result.message);
      await loadLibrary();
    } catch (error) { toast(error.message, true); }
    finally { busy(""); }
  };
  $("deleteButton").onclick = async function () {
    if (this.disabled) return;
    busy(t("Deleting your avatar…"));
    try {
      var result = await api("/api/delete?name=" + encodeURIComponent(state.selected), {method: "POST"});
      busy("");
      closeModal("deleteModal");
      toast(result.message);
      await loadLibrary();
    } catch (error) { toast(error.message, true); }
    finally { busy(""); }
  };
  async function downloadAvatar(name) {
    if (state.busy || !state.connected) return;
    busy(t("Downloading your avatar…"));
    var controller = new AbortController();
    var timer = setTimeout(function () { controller.abort(); }, 30000);
    try {
      var response = await fetch("/api/download?name=" + encodeURIComponent(name), {signal: controller.signal});
      if (!response.ok) {
        var error = await response.json();
        throw new Error(error.message || t("Operation failed."));
      }
      var blob = await response.blob(), url = URL.createObjectURL(blob);
      var link = document.createElement("a");
      link.href = url;
      link.download = name;
      document.body.appendChild(link);
      link.click();
      link.remove();
      setTimeout(function () { URL.revokeObjectURL(url); }, 10000);
    } catch (error) {
      toast(error.name === "AbortError" ? t("The payload took too long to respond. Check your connection.") : error.message, true);
    } finally { clearTimeout(timer); busy(""); }
  }
  applyLanguage();
  connect();
  pollTimer = setInterval(connect, 15000);
})();
