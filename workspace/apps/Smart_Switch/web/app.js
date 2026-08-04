document.addEventListener("DOMContentLoaded", () => {
  // --- API ENDPOINTS ---
  const SETTINGS_API = "/api/settings";
  const SCHEDULES_API = "/api/schedules";

  // --- DOM ELEMENTS ---
  const statusEl = document.getElementById("status");
  // Settings elements
  const nameEl = document.getElementById("name");
  const ssidEl = document.getElementById("wifi_ssid");
  const passwordEl = document.getElementById("wifi_password");
  const timezoneEl = document.getElementById("timezone");
  const maxOnTimeEl = document.getElementById("max_on_time_minutes");
  const saveSettingsBtn = document.getElementById("save-settings-button");
  // Schedule elements
  const showAddFormBtn = document.getElementById("show-add-schedule-form-btn");
  const scheduleFormContainer = document.getElementById("schedule-form-container");
  const scheduleListContainer = document.getElementById("schedule-list-container");

  const setStatus = (message, isError = false) => {
    statusEl.textContent = message;
    statusEl.className = isError ? 'error' : 'success';
  };

  // --- PART 1: DEVICE SETTINGS ---
  const deviceSettingsModule = (() => {
    const load = async () => {
      try {
        const response = await fetch(SETTINGS_API);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const settings = await response.json();
        nameEl.value = settings.name || "";
        ssidEl.value = settings.wifi_ssid || "";
        passwordEl.value = settings.wifi_password || "";
        timezoneEl.value = settings.timezone || "";
        maxOnTimeEl.value = settings.max_on_time_minutes || 0;
      } catch (error) { setStatus(`Failed to load settings: ${error.message}`, true); }
    };

    const save = async () => {
      setStatus("Saving device settings...", false);
      const settings = {
        name: nameEl.value, wifi_ssid: ssidEl.value, wifi_password: passwordEl.value,
        timezone: timezoneEl.value, max_on_time_minutes: Number(maxOnTimeEl.value) || 0
      };
      try {
        const response = await fetch(SETTINGS_API, {
          method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(settings)
        });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        setStatus("Device settings saved successfully!", false);
      } catch (error) { setStatus(`Error saving settings: ${error.message}`, true); }
    };
    return { load, save };
  })();

  // --- PART 2: SCHEDULE MANAGEMENT ---
  const scheduleModule = (() => {
    const weekdaysToMask = (weekdays) => weekdays.reduce((mask, day, i) => mask | (day << i), 0);
    const maskToWeekdays = (mask) => Array.from({ length: 7 }, (_, i) => !!(mask & (1 << i)));

    const load = async () => {
      try {
        const response = await fetch(SCHEDULES_API);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const schedules = await response.json();
        renderList(schedules);
        showAddFormBtn.disabled = schedules.length >= 8;
      } catch (error) { setStatus(`Failed to load schedules: ${error.message}`, true); }
    };

    const renderList = (schedules) => {
      scheduleListContainer.innerHTML = "";
      if (!schedules || schedules.length === 0) {
        scheduleListContainer.innerHTML = "<p>No schedules defined.</p>";
        return;
      }
      schedules.forEach((s, index) => {
        const weekdays = maskToWeekdays(s.day_mask);
        const item = document.createElement("div");
        item.className = "schedule-item";
        item.innerHTML = `
                    <div class="schedule-info">
                        <strong>${String(s.start_hour).padStart(2, '0')}:${String(s.start_minute).padStart(2, '0')} - ${String(s.end_hour).padStart(2, '0')}:${String(s.end_minute).padStart(2, '0')}</strong>
                        <p>${weekdays.map((a, i) => a ? ['S', 'M', 'T', 'W', 'T', 'F', 'S'][i] : null).filter(Boolean).join(', ') || "Once"}</p>
                    </div>
                    <button class="action-btn edit" data-index="${index}" title="Edit"><svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M11 4H4a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-7"></path><path d="M18.5 2.5a2.121 2.121 0 0 1 3 3L12 15l-4 1 1-4 9.5-9.5z"></path></svg></button>
                    <button class="action-btn remove" data-index="${index}" title="Remove"><svg xmlns="http://www.w3.org/2000/svg" width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="3 6 5 6 21 6"></polyline><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path><line x1="10" y1="11" x2="10" y2="17"></line><line x1="14" y1="11" x2="14" y2="17"></line></svg></button>
                `;
        item.querySelector('.edit').addEventListener('click', () => renderForm(s, index));
        item.querySelector('.remove').addEventListener('click', () => remove(index));
        scheduleListContainer.appendChild(item);
      });
    };

    const renderForm = (schedule = null, index = -1) => {
      const isEdit = index !== -1;
      const s = schedule || { enabled: true, day_mask: 0, start_hour: 9, start_minute: 0, end_hour: 17, end_minute: 0 };
      const weekdays = maskToWeekdays(s.day_mask);

      scheduleFormContainer.innerHTML = `
                <div class="schedule-form">
                    <h3>${isEdit ? `Edit Schedule #${index + 1}` : 'Add New Schedule'}</h3>
                    <div class="day-selector">
                        ${['S', 'M', 'T', 'W', 'T', 'F', 'S'].map((day, i) => `<div class="day-toggle ${weekdays[i] ? 'active' : ''}" data-day="${i}"><small>${day}</small></div>`).join('')}
                    </div>
                    <div class="time-row">
                        <div><label>Start</label><input type="time" class="schedule-start" value="${String(s.start_hour).padStart(2, '0')}:${String(s.start_minute).padStart(2, '0')}"></div>
                        <div><label>End</label><input type="time" class="schedule-end" value="${String(s.end_hour).padStart(2, '0')}:${String(s.end_minute).padStart(2, '0')}"></div>
                    </div>
                    <div class="button-group">
                        <button class="button-primary">${isEdit ? 'Update' : 'Save'}</button>
                        <button class="button-secondary">Cancel</button>
                    </div>
                </div>`;
      showAddFormBtn.style.display = 'none';

      const form = scheduleFormContainer.querySelector('.schedule-form');
      form.querySelector('.button-primary').addEventListener('click', () => save(form, index));
      form.querySelector('.button-secondary').addEventListener('click', () => {
        scheduleFormContainer.innerHTML = "";
        showAddFormBtn.style.display = 'block';
      });
      form.querySelectorAll('.day-toggle').forEach(t => t.addEventListener('click', () => t.classList.toggle('active')));
    };

    const save = async (form, index) => {
      const isEdit = index !== -1;
      const weekdays = Array.from(form.querySelectorAll('.day-toggle')).map(t => t.classList.contains('active'));
      const [start_hour, start_minute] = form.querySelector('.schedule-start').value.split(':').map(Number);
      const [end_hour, end_minute] = form.querySelector('.schedule-end').value.split(':').map(Number);

      const scheduleData = { enabled: true, day_mask: weekdaysToMask(weekdays), start_hour, start_minute, end_hour, end_minute };

      try {
        const url = isEdit ? `${SCHEDULES_API}/${index}` : SCHEDULES_API;
        const method = isEdit ? 'PUT' : 'POST';
        const response = await fetch(url, { method, headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(scheduleData) });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        setStatus(`Schedule ${isEdit ? 'updated' : 'added'} successfully!`, false);
        scheduleFormContainer.innerHTML = "";
        showAddFormBtn.style.display = 'block';
        await load();
      } catch (error) { setStatus(`Error saving schedule: ${error.message}`, true); }
    };

    const remove = async (index) => {
      if (!confirm(`Are you sure you want to delete Schedule #${index + 1}?`)) return;
      try {
        const response = await fetch(`${SCHEDULES_API}/${index}`, { method: 'DELETE' });
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        setStatus(`Schedule #${index + 1} deleted.`, false);
        await load();
      } catch (error) { setStatus(`Error deleting schedule: ${error.message}`, true); }
    };

    return { load, renderForm };
  })();

  // --- INITIALIZATION ---
  saveSettingsBtn.addEventListener('click', deviceSettingsModule.save);
  showAddFormBtn.addEventListener('click', () => scheduleModule.renderForm());

  // Load all data on page start
  setStatus("Loading all configurations...", false);
  Promise.all([deviceSettingsModule.load(), scheduleModule.load()]).then(() => {
    setStatus("Configuration loaded.", false);
  });
});
