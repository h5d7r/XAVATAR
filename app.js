/* XAVATAR has no CDN dependencies. Images stay between this browser and the PS4. */
"use strict";
(function () {
  var $ = function (id) {
    return document.getElementById(id);
  };
  var state = {
    mode: "both",
    connected: false,
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
    label.textContent = text;
    var close = document.createElement("button");
    close.textContent = "×";
    close.setAttribute("aria-label", "Dismiss notification");
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
          result.message || "The payload could not complete this operation.",
        );
      return result;
    } catch (error) {
      if (error.name === "AbortError")
        throw new Error(
          "The payload took too long to respond. Check your connection.",
        );
      throw error;
    } finally {
      if (timer) clearTimeout(timer);
    }
  }

  function busy(text, detail) {
    state.busy = !!text;
    $("busyOverlay").hidden = !text;
    $("busyText").textContent = text || "";
    $("busyDetail").textContent = detail || "Keep this page open.";
    $("uploadProgress").hidden = true;
    document.body.setAttribute("aria-busy", text ? "true" : "false");
    updateAction();
  }

  function setConnection(online, test) {
    state.connected = online;
    $("connection").classList.toggle("online", online);
    $("connectionText").textContent = online
      ? test
        ? "Host test connected"
        : "Payload connected"
      : state.stopped
        ? "Service stopped"
        : "Payload offline";
    $("connectionBanner").hidden = online;
    $("connectionMessage").textContent = state.stopped
      ? "XAVATAR is stopped. Load the payload again on your PS4 to reopen port 4002."
      : "Cannot reach the payload. Check that XAVATAR is running on your PS4.";
    $("reconnect").textContent = state.stopped ? "Check again" : "Reconnect";
    $("footerStatus").textContent = online
      ? test
        ? "LOCAL TEST · PORT 4002"
        : "CONNECTED · PORT 4002"
      : state.stopped
        ? "STOPPED · PORT 4002 CLOSED"
        : "PAYLOAD OFFLINE";
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
      ? "Select a PS4 user"
      : "No PS4 users available";
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
    $("userHelp").textContent = "Loading usernames…";
    $("refreshUsers").disabled = true;
    userLoadPromise = (async function () {
      try {
        var result = await api("/api/users");
        state.users = result.users;
        populateUsers($("userSelect"), $("userSelect").value);
        populateUsers($("installUser"), $("installUser").value);
        $("userHelp").textContent = state.users.length
          ? "Select the profile for your new look."
          : "No local users found. Refresh after signing in on your PS4.";
      } catch (error) {
        state.users = [];
        populateUsers($("userSelect"));
        populateUsers($("installUser"));
        $("userHelp").textContent =
          "Could not load usernames. Use Refresh to try again.";
        toast("Loading usernames failed: " + error.message, true);
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
    $("librarySummary").textContent = "Loading avatars…";
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
        $("librarySummary").textContent = "Library unavailable";
        $("libraryError").textContent =
          "Could not load avatars: " + error.message;
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
        ? "No matching avatars."
        : "Your next look starts here.";
      $("libraryEmpty").querySelector("p").textContent = term
        ? "Try another name or clear your search."
        : "Create and save your first avatar to start your collection.";
      $("emptyCreate").hidden = !!term;
    }
    $("librarySummary").textContent =
      avatars.length +
      (avatars.length === 1 ? " avatar" : " avatars") +
      (term ? " found" : " in your collection");
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
      button.textContent = "Install avatar →";
      button.onclick = function () {
        openInstall(avatar.name);
      };
      img.onerror = function () {
        img.hidden = true;
        badge.textContent = "INVALID PACKAGE";
        button.disabled = true;
        meta.textContent = "Preview unavailable · check this package";
      };
      thumb.appendChild(img);
      thumb.appendChild(badge);
      info.appendChild(name);
      info.appendChild(meta);
      info.appendChild(button);
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
        ? "INSTALL + SAVE"
        : value === "save"
          ? "SAVE TO LIBRARY"
          : "DIRECT INSTALL";
    $("createButton").querySelector("span").textContent =
      value === "both"
        ? "Create & install"
        : value === "save"
          ? "Create XAVATAR file"
          : "Install without saving";
    updateAction();
  }

  function validName() {
    return /^[A-Za-z0-9_-][A-Za-z0-9 _-]{0,91}$/.test(
      $("filename").value.trim(),
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
    var name = state.users.find(function (u) {
      return u.id === $("userSelect").value;
    });
    $("previewName").textContent =
      name && needUser ? name.name : "Your next identity";
    $("installButton").disabled =
      !state.connected || !$("installUser").value || state.busy;
    $("actionHint").textContent = !state.connected
      ? "Connect to the payload to continue."
      : !state.crop
        ? "Choose an image to get started."
        : needUser && !$("userSelect").value
          ? "Select the PS4 user for this avatar."
          : needName && !validName()
            ? "Use letters, numbers, spaces, hyphens or underscores for the name."
            : state.mode === "install"
              ? "Applied directly. No .xavatar file will be saved."
              : state.mode === "save"
                ? "Your package will be ready in the avatar library."
                : "Apply it now. Keep it in your library.";
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
  function loadImage(file) {
    if (!file || state.busy) return;
    if (!/^image\/(png|jpeg|webp|gif)$/.test(file.type)) {
      toast("Choose a PNG, JPG, WebP or GIF image.", true);
      return;
    }
    if (file.size > 20 * 1024 * 1024) {
      toast("Choose an image smaller than 20 MB.", true);
      return;
    }
    busy("Loading your image…", "Preparing the crop editor.");
    var url = URL.createObjectURL(file),
      image = new Image();
    image.onload = function () {
      if (image.naturalWidth * image.naturalHeight > 32000000) {
        URL.revokeObjectURL(url);
        busy("");
        toast(
          "This image is too large. Choose an image below 32 megapixels.",
          true,
        );
        return;
      }
      snapshotCrop();
      state.source = image;
      state.sourceUrl = url;
      state.filename = file.name;
      busy("");
      resetCrop();
      openModal("cropModal");
    };
    image.onerror = function () {
      URL.revokeObjectURL(url);
      busy("");
      toast("This image could not be opened. Try another image.", true);
    };
    image.src = url;
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
    busy("Processing your crop…", "Preparing a 440 × 440 image.");
    canvas.toBlob(function (blob) {
      if (!blob) {
        busy("");
        toast("Could not process the image. Please try again.", true);
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
      toast("Crop ready. Your next look is taking shape.");
    }, "image/png");
  };

  function upload(path, blob) {
    return new Promise(function (resolve, reject) {
      var xhr = new XMLHttpRequest();
      xhr.open("POST", path);
      xhr.timeout = 120000;
      xhr.setRequestHeader("Content-Type", "image/png");
      xhr.setRequestHeader("X-XAVATAR", "1");
      xhr.upload.onprogress = function (event) {
        if (event.lengthComputable) {
          $("uploadProgress").hidden = false;
          $("uploadProgress").value = (event.loaded / event.total) * 100;
          $("busyDetail").textContent =
            "Sending your cropped image… " +
            Math.round((event.loaded / event.total) * 100) +
            "%";
        }
      };
      xhr.upload.onload = function () {
        $("uploadProgress").hidden = true;
        $("busyDetail").textContent =
          state.mode === "install"
            ? "Creating textures and installing your avatar…"
            : state.mode === "save"
              ? "Creating textures and saving your package…"
              : "Creating textures, saving and installing your avatar…";
      };
      xhr.onload = function () {
        try {
          var result = JSON.parse(xhr.responseText);
          if (xhr.status < 200 || xhr.status >= 300)
            throw new Error(result.message || "Operation failed.");
          resolve(result);
        } catch (error) {
          reject(error);
        }
      };
      xhr.onerror = function () {
        reject(
          new Error(
            "Connection lost. Reconnect and check the library before retrying.",
          ),
        );
      };
      xhr.ontimeout = function () {
        reject(
          new Error(
            "The operation timed out. Check the library and profile before retrying.",
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
        ? "Creating your XAVATAR file…"
        : state.mode === "install"
          ? "Installing your new look…"
          : "Creating & installing your avatar…",
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
      "Installing your avatar…",
      "Applying your chosen look to the selected PS4 user.",
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
    busy("Stopping XAVATAR…", "Closing port 4002 and ending the payload.");
    try {
      await api("/api/stop", { method: "POST" });
      state.stopped = true;
      clearInterval(pollTimer);
      pollTimer = null;
      setConnection(false);
      toast("XAVATAR stopped. Load the payload again to reopen port 4002.");
    } catch (error) {
      toast("Could not confirm shutdown: " + error.message, true);
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
  mode("both");
  connect();
  pollTimer = setInterval(connect, 15000);
})();
