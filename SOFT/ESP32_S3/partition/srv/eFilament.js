
// Глобальное состояние
const AppState = {
    profiles: [],
    activeProfileId: null,
    activeProfileName: null,
    currentProfile: null,
    availableTypes: [],
    availableVendors: [],
    availableInfos: [],
    availableInfos2: [],
    availableFullWeights: [],
    availableSpoolWeights: [],
    availableDensities: [],
    availableDiameters: []
};

// Универсальная функция для работы с профилями
function profilesRequest(type, data = null, callback = null) {
    const url = `/profiles?type=${type}`;
    const requestData = {
        url: url,
        type: 'POST', // Все запросы POST для единообразия
        dataType: 'json'
    };

    // Добавляем данные если есть
    if (data) {
        requestData.contentType = 'application/json';
        requestData.data = JSON.stringify(data);
    }

    $.ajax(requestData)
        .done(function(response) {
            if (callback) callback(response);
        })
        .fail(function(xhr, status, error) {
            console.error(`Profiles request failed (${type}):`, error);
            showStatus(`Operation failed: ${error}`, 'error');
        });
}

// Загрузка профилей

function loadProfiles() {
    console.log('Loading profiles...');
    
    profilesRequest('load', null, function(response) {
        console.log('Profiles response:', response);
        
        AppState.profiles = response.profiles || [];
        AppState.activeProfileId = response.active_profile || null;
       
        // Собираем уникальные значения для всех полей
        collectUniqueValues();
        updateProfilesSelect();
        updateEditableSelects();
        
        // Выбираем активный профиль
        if (AppState.activeProfileId) {
            console.log('Setting active profile:', AppState.activeProfileId);
            
            // Добавляем небольшую задержку для гарантии обновления DOM
            setTimeout(() => {
                $('#profilesSelect').val(AppState.activeProfileId);
                onProfileSelect();
                AppState.activeProfileName = AppState.profiles.find(p => p.id === AppState.activeProfileId);
                // Дополнительная проверка
                const currentVal = $('#profilesSelect').val();
                console.log('Current select value after setting:', currentVal);
                
                if (currentVal !== AppState.activeProfileId) {
                    console.warn('Active profile was not set correctly');
                    // Пытаемся установить снова
                    setTimeout(() => {
                        $('#profilesSelect').val(AppState.activeProfileId);
                        onProfileSelect();
                    }, 200);
                }
            }, 100);
       
        } else {
            console.log('No active profile ID found');
        }
        setActiveProfile();
       // showStatus('Profiles loaded successfully', 'success');
    });
}

// Обновленная функция updateProfilesSelect
function updateProfilesSelect() {
   const select = $('#profilesSelect');
    select.empty();
    
    // Сначала заполняем все опции
    AppState.profiles.forEach(profile => {
        const isActive = profile.id === AppState.activeProfileId;
        const optionText = isActive ? `★ ${profile.name}` : profile.name;
        select.append(new Option(optionText, profile.id));
    });
    
    // Затем устанавливаем активное значение
    if (AppState.activeProfileId) {
        setTimeout(() => {                          // Используем setTimeout чтобы дать DOM обновиться
            select.val(AppState.activeProfileId).selectmenu('refresh');
            console.log('Active profile set to:', AppState.activeProfileId);
            select.trigger('change');               // Принудительно запускаем событие change
        }, 0);
    }
    console.log('Select updated, current value:', select.val());
}

// Установка активного профиля
function setActiveProfile() {
    const profileId = $('#profilesSelect').val();
    if (!profileId) return;
    
    profilesRequest('setactive', { id: profileId }, function(response) {
        
        AppState.activeProfileId = profileId;
        updateProfilesSelect();
        const monProfileName = $('#monProfileName')
        monProfileName.text(AppState.currentProfile.name);
        showStatus('Profile set as active', 'success');

    });
}

// Сохранение профиля (редактирование или добавление)
// Обновленная функция saveProfile
function saveProfile(isEdit) {
    
    const formData = {
        old_id: $('#modalOldId').val(),
        id: $('#modalId').val(),
        type: $('#modalType').val(),
        vendor: $('#modalVendor').val(),
        info: $('#modalInfo').val(),
        info2: $('#modalInfo2').val(),
        full_weight: parseInt($('#modalFullWeight').val()),
        spool_weight: parseInt($('#modalSpoolWeight').val()),
        density: parseFloat($('#modalDensity').val()),
        diameter: parseFloat($('#modalDiameter').val())

    };
    
    console.log('Saving profile:', formData);
    
    // Валидация данных
    if (!validateProfileData(formData)) {
        return;
    }
    
    const requestType = isEdit ? 'update' : 'add';
    
    profilesRequest(requestType, formData, function(response) {
        console.log('Save response:', response);
                
        loadProfiles(); // Перезагружаем список
        if (response.Status == 'OK') {
            showStatus(response.Message, 'success');
            loadProfiles();
        }
        if (response.Status == 'FAIL') {
            showStatus(response.Message, 'error');
        }
        // Обновляем Editable Select после сохранения
        setTimeout(updateEditableSelects, 200);
    });
}

// Удаление профиля
function deleteProfile() {
    const profileId = $('#profilesSelect').val();
    if (!profileId) return;
    
    if (!confirm('Are you sure you want to delete this profile?')) {
        return;
    }
    
    profilesRequest('delete', { id: profileId }, function(response) {
        loadProfiles(); // Перезагружаем список
        showStatus('Profile deleted', 'success');
    });
}

// Остальные функции остаются без изменений
function collectUniqueValues() {
    AppState.availableTypes = [];
    AppState.availableVendors = [];
    AppState.availableInfos = [];
    AppState.availableInfos2 = [];
    AppState.availableFullWeights = [];
    AppState.availableSpoolWeights = [];
    AppState.availableDensities = [];
    AppState.availableDiameters = [];
    
    AppState.profiles.forEach(profile => {
        addUniqueValue(AppState.availableTypes, profile.type);
        addUniqueValue(AppState.availableVendors, profile.vendor);
        addUniqueValue(AppState.availableInfos, profile.info);
        addUniqueValue(AppState.availableInfos2, profile.info2);
        addUniqueValue(AppState.availableFullWeights, profile.full_w.toString());
        addUniqueValue(AppState.availableSpoolWeights, profile.spool_w.toString());
        addUniqueValue(AppState.availableDensities, profile.density.toString());
        addUniqueValue(AppState.availableDiameters, profile.dia.toString());
    });
    
    // Сортируем числовые значения
    AppState.availableFullWeights.sort((a, b) => a - b);
    AppState.availableSpoolWeights.sort((a, b) => a - b);
    AppState.availableDensities.sort((a, b) => a - b);
    AppState.availableDiameters.sort((a, b) => a - b);
}

function addUniqueValue(array, value) {
    if (value && value !== "" && !array.includes(value)) {
        array.push(value);
    }
}

// Улучшенная функция updateEditableSelect
function updateEditableSelect(selector, values) {
    const element = $(selector);
    
    // Убедимся, что элемент существует
    if (element.length === 0) {
        console.error(`Element ${selector} not found`);
        return;
    }
    
    // Если уже инициализирован, обновляем
    if (element.data('editableSelect')) {
        element.editableSelect('clear');
        values.forEach(value => {
            if (value && value !== '') {
                element.editableSelect('add', value);
            }
        });
    } else {
        // Инициализируем впервые
        element.editableSelect({
            source: values.filter(v => v && v !== ''),
            filter: true,
            duration: 0
        });
    }
}

//function updateProfilesSelect() {
//    const select = $('#profilesSelect');
//    select.empty();
//  //  select.append('<option value="">Select a profile...</option>');
    
//    AppState.profiles.forEach(profile => {
//        const isActive = profile.id === AppState.activeProfileId;
//        const optionText = isActive ? `★ ${profile.name}` : profile.name;
//        select.append(new Option(optionText, profile.id));
//    });
//}

function onProfileSelect() {
    const selectedId = $('#profilesSelect').val();
    const hasSelection = !!selectedId;
    
    $('#setActiveBtn').prop('disabled', !hasSelection);
    $('#editProfileBtn').prop('disabled', !hasSelection);
    $('#deleteProfileBtn').prop('disabled', !hasSelection);
    
    if (hasSelection) {
        const profile = AppState.profiles.find(p => p.id === selectedId);
        if (profile) {
            AppState.currentProfile = profile;
            showProfileDetails(profile);
        }
    } else {
        $('#profileDetails').hide();
        AppState.currentProfile = null;
    }
}

// Обновленная функция showProfileDetails
function showProfileDetails(profile) {
    $('#detailId').text(profile.id);
    $('#detailType').text(profile.type);
    $('#detailVendor').text(profile.vendor);
    $('#detailInfo').text(profile.info);
    $('#detailInfo2').text(profile.info2);
    $('#detailFullWeight').text(profile.full_w + ' g');
    $('#detailSpoolWeight').text(profile.spool_w + ' g');
    
    // Форматируем density до 2 знаков
    const formattedDensity = typeof profile.density === 'number' 
        ? profile.density.toFixed(2) 
        : parseFloat(profile.density).toFixed(2);
    $('#detailDensity').text(formattedDensity + ' g/cm³');
    
    $('#detailDiameter').text(profile.dia + ' mm');
    
    $('#profileDetails').show();
}

// Обновленная функция для сбора уникальных значений
function collectUniqueValues() {
    AppState.availableTypes = [];
    AppState.availableVendors = [];
    AppState.availableInfos = [];
    AppState.availableInfos2 = [];
    AppState.availableFullWeights = [];
    AppState.availableSpoolWeights = [];
    AppState.availableDensities = [];
    AppState.availableDiameters = [];
    
    AppState.profiles.forEach(profile => {
        addUniqueValue(AppState.availableTypes, profile.type);
        addUniqueValue(AppState.availableVendors, profile.vendor);
        addUniqueValue(AppState.availableInfos, profile.info);
        addUniqueValue(AppState.availableInfos2, profile.info2);
        addUniqueValue(AppState.availableFullWeights, profile.full_w.toString());
        addUniqueValue(AppState.availableSpoolWeights, profile.spool_w.toString());
        
        // Форматируем density при сборе уникальных значений
        const formattedDensity = typeof profile.density === 'number' 
            ? profile.density.toFixed(2) 
            : parseFloat(profile.density).toFixed(2);
        addUniqueValue(AppState.availableDensities, formattedDensity);
        
        addUniqueValue(AppState.availableDiameters, profile.dia.toString());
    });
    
    // Сортируем числовые значения
    AppState.availableFullWeights.sort((a, b) => a - b);
    AppState.availableSpoolWeights.sort((a, b) => a - b);
    AppState.availableDensities.sort((a, b) => a - b);
    AppState.availableDiameters.sort((a, b) => a - b);
}

// Обновленная функция openEditModal
function openEditModal() {
    if (!AppState.currentProfile) {
        console.error('No current profile selected');
        return;
    }
    
    console.log('Opening edit modal for:', AppState.currentProfile);
    
    // Обновляем Editable Select перед открытием
    updateEditableSelects();
    
    // Заполняем форму данными текущего профиля
    $('#modalOldId').val(AppState.currentProfile.id).prop('readonly', true);
    $('#modalId').val(AppState.currentProfile.id);
    $('#modalType').val(AppState.currentProfile.type);
    $('#modalVendor').val(AppState.currentProfile.vendor);
    $('#modalInfo').val(AppState.currentProfile.info);
    $('#modalInfo2').val(AppState.currentProfile.info2);
    $('#modalFullWeight').val(AppState.currentProfile.full_w);
    $('#modalSpoolWeight').val(AppState.currentProfile.spool_w);
    
    // Форматируем density для модального окна
    const formattedDensity = typeof AppState.currentProfile.density === 'number' 
        ? AppState.currentProfile.density.toFixed(2) 
        : parseFloat(AppState.currentProfile.density).toFixed(2);
    $('#modalDensity').val(formattedDensity);
    
    $('#modalDiameter').val(AppState.currentProfile.dia);
    
    // Открываем модальное окно
    $('#profileModal').dialog({
        modal: true,
        width: 500,
        title: 'Edit Profile',
        open: function() {
            console.log('Edit modal opened');
            // Дополнительная инициализация после открытия
            updateEditableSelects();
        },
        buttons: {
            Save: function() {
                if (!validateAllEditableSelects()) {
                   showStatus('Please fix validation errors', 'error');
                return;
                }
                saveProfile(true);
                $(this).dialog('close');
            },
            Cancel: function() {
                $(this).dialog('close');
            }
        }
    });
}

function updateEditableSelects() {
    // Добавляем небольшую задержку для гарантии инициализации
    setTimeout(() => {
        updateEditableSelect('#modalType', AppState.availableTypes);
        updateEditableSelect('#modalVendor', AppState.availableVendors);
        updateEditableSelect('#modalInfo', AppState.availableInfos);
        updateEditableSelect('#modalInfo2', AppState.availableInfos2);
        updateEditableSelect('#modalFullWeight', AppState.availableFullWeights);
        updateEditableSelect('#modalSpoolWeight', AppState.availableSpoolWeights);
        updateEditableSelect('#modalDensity', AppState.availableDensities);
        updateEditableSelect('#modalDiameter', AppState.availableDiameters);
    }, 100);

        // Добавляем валидацию
    initEditableSelectWithValidation('#modalId', '^[A-Z0-9]{3,4}$', '3-4 uppercase letters/numbers');
    initEditableSelectWithValidation('#modalFullWeight', '^[0-9]+$', 'Numbers only');
    initEditableSelectWithValidation('#modalSpoolWeight', '^[0-9]+$', 'Numbers only');
    initEditableSelectWithValidation('#modalDensity', '^[0-9]+\\.?[0-9]*$', 'Decimal number');
    initEditableSelectWithValidation('#modalDiameter', '^[0-9]+\\.?[0-9]*$', 'Decimal number');

}

function updateEditableSelect(selector, values) {
    const element = $(selector);
    
    // Убедимся, что элемент существует
    if (element.length === 0) {
        console.error(`Element ${selector} not found`);
        return;
    }
    
    // Если уже инициализирован, обновляем
    if (element.data('editableSelect')) {
        element.editableSelect('clear');
        values.forEach(value => {
            if (value && value !== '') {
                element.editableSelect('add', value);
            }
        });
    } else {
        // Инициализируем впервые
        element.editableSelect({
            source: values.filter(v => v && v !== ''),
            filter: true,
            duration: 0
        });
    }
}
// Обновленная функция openAddModal
function openAddModal() {
    console.log('Opening add modal');
    
    // Обновляем Editable Select перед открытием
    //updateEditableSelects();
    
    // Проверяем, что форма существует перед вызовом reset()
    const profileForm = $('#profileForm');
    if (profileForm.length > 0) {
        profileForm[0].reset();
    } else {
        console.error('Profile form not found!');
        return;
    }
    
    $('#modalId').prop('readonly', false);
    
    // Открываем модальное окно
    $('#profileModal').dialog({
        modal: true,
        width: 500,
        title: 'Add New Profile',
        open: function() {
            console.log('Add modal opened');
            // Дополнительная инициализация после открытия
            updateEditableSelects();
        },
        buttons: {
            Save: function() {
                saveProfile(false);
                $(this).dialog('close');
            },
            Cancel: function() {
                $(this).dialog('close');
            }
        }
    });
}

function validateProfileData(data) {
    if (!/^[A-Z0-9]{3,4}$/.test(data.id)) {
        alert('ID must be 3-4 uppercase letters or numbers');
        return false;
    }
    
    if (data.full_weight <= 0 || data.spool_weight <= 0) {
        alert('Weights must be positive numbers');
        return false;
    }
    
    if (data.density <= 0 || data.diameter <= 0) {
        alert('Density and diameter must be positive numbers');
        return false;
    }
    
    if (data.spool_weight >= data.full_weight) {
        alert('Spool weight must be less than full weight');
        return false;
    }
    
    return true;
}

function initEditableSelectWithValidation(selector, pattern, errorMessage) {
    const element = $(selector);
    
    element.editableSelect({
        filter: true,
        duration: 0
    }).on('select.editable-select', function(e) {
        validateEditableSelect($(this), pattern, errorMessage);
    }).on('close.editable-select', function(e) {
        validateEditableSelect($(this), pattern, errorMessage);
    });
    
    // Также валидируем при ручном вводе
    element.closest('.es-list').find('input').on('input', function() {
        setTimeout(() => {
            validateEditableSelect(element, pattern, errorMessage);
        }, 100);
    });
}

function validateEditableSelect($element, pattern, errorMessage) {
    const value = $element.val();
    const regex = new RegExp(pattern);
    const $wrapper = $element.closest('.es-wrapper');
    
    // Удаляем предыдущие сообщения об ошибках
    $wrapper.find('.validation-error').remove();
    
    if (value && !regex.test(value)) {
        $element.addClass('validation-error');
        $wrapper.append(`<div class="validation-error" style="color: red; font-size: 12px; margin-top: 5px;">${errorMessage}</div>`);
        return false;
    } else {
        $element.removeClass('validation-error');
        return true;
    }
}
function validateAllEditableSelects() {
    let isValid = true;
    
    // Валидируем ID
    if (!validateEditableSelect($('#modalId'), '^[A-Z0-9]{3,4}$', 'Invalid ID')) {
        isValid = false;
    }
    
    // Валидируем числовые поля
    if (!validateEditableSelect($('#modalFullWeight'), '^[0-9]+$', 'Invalid weight')) {
        isValid = false;
    }
    
    if (!validateEditableSelect($('#modalSpoolWeight'), '^[0-9]+$', 'Invalid weight')) {
        isValid = false;
    }
    
    if (!validateEditableSelect($('#modalDensity'), '^[0-9]+\\.?[0-9]*$', 'Invalid density')) {
        isValid = false;
    }
    
    if (!validateEditableSelect($('#modalDiameter'), '^[0-9]+\\.?[0-9]*$', 'Invalid diameter')) {
        isValid = false;
    }
    
    return isValid;
}

// Инициализация при загрузке страницы
$(document).ready(function() {
    // Обработчики событий
    $('#profilesSelect').selectmenu({ change: onProfileSelect });
    $('#setActiveBtn').click(setActiveProfile);
    $('#editProfileBtn').click(openEditModal);
    $('#addProfileBtn').click(openAddModal);
    $('#deleteProfileBtn').click(deleteProfile);
    
    // Загружаем профили
    loadProfiles();
    
    // Предотвращаем отправку формы по Enter
    $('#profileForm').on('keypress', function(e) {
        if (e.which === 13) {
            e.preventDefault();
        }
    });
});

function updateDebugInfo() {
    if ($('#debugInfo').is(':visible')) {
        $('#debugContent').html(`
            Active Profile ID: ${AppState.activeProfileId}<br>
            Profiles Count: ${AppState.profiles.length}<br>
            Current Select Value: ${$('#profilesSelect').val()}<br>
            Available Types: ${AppState.availableTypes.join(', ')}<br>
            Available Vendors: ${AppState.availableVendors.join(', ')}
        `);
    }
}
// Функция для показа статусных сообщений
function showStatus(message, type) {
    var statusDiv = $('#uploadStatus');
    statusDiv.removeClass('success error')
             .addClass(type)
             .text(message)
             .show();
    
    setTimeout(function() {
        statusDiv.fadeOut();
    }, 5000);
}
// ------------------- end profiles --------------------------
$(function() {
      // Инициализация табов
      $("#tabsMain").tabs();
      progressLabel = $( "#monitor-progress-label" );
    $(function () {
        $("#profilesSelect").selectmenu({width: 350});
				  });
	
	  $("#MonitorProgressBar").progressbar({ 
		  value: 0,
		   change: function() {
      	 var value = $(this).progressbar("value");
    //    $("#monitor-progress-label").text(value + "%");
        
        // Удаляем все цветовые классы
        $(this).removeClass("progress-low progress-medium progress-high progress-critical");
        
        // Добавляем класс в зависимости от значения
        if (value < 10) {
            $(this).addClass("progress-critical");
        } else if (value < 25) {
            $(this).addClass("progress-low");
        } else if (value < 50) {
            $(this).addClass("progress-medium");
        } else {
            $(this).addClass("progress-high");
        }
      },
	  });
      // Инициализация спиннера и прогресс-баров
      var spinner = $("#wgtInput").spinner();
      $("#zeroProgressBar").progressbar({ value: 0 });
      $("#fullscaleProgressBar").progressbar({ value: 0 });
      
      // Переменные для хранения интервалов опроса прогресса
      var zeroProgressInterval;
      var fullscaleProgressInterval;
	  setInterval(updateMonitor, 1000);
      // Обработчик выбора файла
      $('#fileInput').change(function() {
        var file = this.files[0];
        if (file) {
          $('#fileName').text(file.name);
          $('#uploadFile')
            .prop('disabled', false)
            .addClass('upload-active');
        } else {
          $('#fileName').text('No file chosen');
          $('#uploadFile')
            .prop('disabled', true)
            .removeClass('upload-active');
        }
        
      });

      // Обработчик загрузки файла
      $("#uploadFile").click(function() {
        var fileInput = document.getElementById('fileInput');
        
        if (!fileInput || !fileInput.files || fileInput.files.length === 0) {
          showStatus('Please select a file first!', 'error');
          return;
        }
        
        var file = fileInput.files[0];
        
        // Показываем индикатор загрузки
        $('#uploadFile').prop('disabled', true).text('Uploading...');
        
        // Создаем FormData объект
        var formData = new FormData();
        formData.append('file', file);
        
        // Отправляем AJAX запрос
        $.ajax({
          url: '/upload',
          type: 'POST',
          data: formData,
          processData: false,
          contentType: false,
          success: function(response) {
            showStatus('File uploaded successfully!', 'success');
            
            // Очищаем поле с именем файла
            $('#fileName').text('No file chosen');
            
            // Сбрасываем input file
            $('#fileInput').val('');
            
            // Возвращаем кнопку в исходное состояние
            $('#uploadFile')
              .text('Upload')
              .prop('disabled', true)
              .removeClass('upload-active');
          },
          error: function(xhr, status, error) {
            showStatus('Upload failed: ' + error, 'error');
            $('#uploadFile')
              .text('Upload')
              .prop('disabled', false)
              .addClass('upload-active');
          }
        });
      });
      
      // Обработчик калибровки нуля
      $("#butt_calibZero").click(function() {
        // Блокируем кнопку
        $(this).prop('disabled', true).text('Calibrating...');
        
        // Показываем прогресс-бар
        $("#zeroProgressContainer").show();
        $("#zeroProgressBar").progressbar("value", 0);
        
        // Запускаем калибровку
        $.ajax({
          url: '/calibrate_zero',
          type: 'POST',
          success: function(response) {
            // Запускаем опрос прогресса
            zeroProgressInterval = setInterval(checkZeroProgress, 1000);
          },
          error: function(xhr, status, error) {
            showStatus('Zero calibration failed: ' + error, 'error');
            $("#butt_calibZero").prop('disabled', false).text('Calibrate Zero');
            $("#zeroProgressContainer").hide();
          }
        });
      });
      
      // Обработчик калибровки полной шкалы
      $("#butt_calibFullscale").click(function() {
        var weight = $("#wgtInput").val();
        if (!weight || weight <= 0) {
          showStatus('Please enter a valid weight!', 'error');
          return;
        }
        
        // Блокируем кнопку
        $(this).prop('disabled', true).text('Calibrating...');
        
        // Показываем прогресс-бар
        $("#fullscaleProgressContainer").show();
        $("#fullscaleProgressBar").progressbar("value", 0);
        
        // Запускаем калибровку
        $.ajax({
          url: '/calibrate_fullscale',
          type: 'POST',
          contentType: 'application/json',
		  data: JSON.stringify({reference_weight: weight}),
          success: function(response) {
            // Запускаем опрос прогресса
            fullscaleProgressInterval = setInterval(checkFullscaleProgress, 1000);
          },
          error: function(xhr, status, error) {
            showStatus('Fullscale calibration failed: ' + error, 'error');
            $("#butt_calibFullscale").prop('disabled', false).text('Calibrate Fullscale');
            $("#fullscaleProgressContainer").hide();
          }
        });
      });
      
      // Функция проверки прогресса калибровки нуля
      function checkZeroProgress() {
        $.ajax({
          url: '/calibrate_zero_progress',
          type: 'GET',
          success: function(response) {
            var progress = parseInt(response.progress) || 0;
            $("#zeroProgressBar").progressbar("value", progress);
            
            if (progress >= 100) {
              clearInterval(zeroProgressInterval);
              showStatus('Zero calibration completed successfully!', 'success');
              $("#butt_calibZero").prop('disabled', false).text('Calibrate Zero');
              // Скрываем прогресс через 2 секунды после завершения
              setTimeout(function() {
                $("#zeroProgressContainer").hide();
              }, 2000);
            }
          },
          error: function() {
            // В случае ошибки опроса, просто продолжаем
          }
        });
      }
      
      // Функция проверки прогресса калибровки полной шкалы
      function checkFullscaleProgress() {
        $.ajax({
          url: '/calibrate_fullscale_progress',
          type: 'GET',
          success: function(response) {
            var progress = parseInt(response.progress) || 0;
            $("#fullscaleProgressBar").progressbar("value", progress);
            
            if (progress >= 100) {
              clearInterval(fullscaleProgressInterval);
              showStatus('Fullscale calibration completed successfully!', 'success');
              $("#butt_calibFullscale").prop('disabled', false).text('Calibrate Fullscale');
              // Скрываем прогресс через 2 секунды после завершения
              setTimeout(function() {
                $("#fullscaleProgressContainer").hide();
              }, 2000);
            }
          },
          error: function() {
            // В случае ошибки опроса, просто продолжаем
          }
        });
      }
     // Функция монитора
      function updateMonitor() {
        $.ajax({
          url: '/monitor',
          type: 'GET',
		  dataType: 'json',
      	  success: function(response) {
		  if(response.prc != -1){			  
		  $("#MonitorProgressBar").progressbar("value", response.prc);	  
		  $("#mon_prc").text(response.prc + "." + response.prc_fl + "%");
	      $("#mon_wgt").text(response.wgt + " g");
		  $("#mon_lgt").text(response.lgt + " m");
		  $("#mon_wgt_total").text(response.wgt_total + " g");
					  
		  } else {
		  $("#MonitorProgressBar").progressbar("value", 0);	  
		  $("#mon_prc").text("--.-- %");
	      $("#mon_wgt").text("-- g");
		  $("#mon_lgt").text("-- m");
		  $("#mon_wgt_total").text("-- g");
		  }
          const monProfileName = $('#monProfileName');
          
          if ((AppState.activeProfileId != response.active_prifile_id) || (monProfileName.text() != AppState.activeProfileName)) 
          {
               AppState.activeProfileId = response.active_prifile_id;
               updateProfilesSelect();

                const profile = AppState.profiles.find(p => p.id === response.active_prifile_id);
                if (profile) {
                    AppState.currentProfile = profile;
                    AppState.activeProfileName = profile.name;
                    showProfileDetails(profile);
          }

               
               monProfileName.text(AppState.currentProfile.name);
               showStatus('Profile set as active', 'success');
          }
      },
          error: function() {
            // В случае ошибки опроса, просто продолжаем
          }
        });
      }
      // Обработчик скачивания
      $("#downloadFile").click(function() {
        window.open('/download', '_blank');
      });

      // Функция для показа статуса
      //function showStatus(message, type) {
      //  var statusDiv = $('#uploadStatus');
      //  statusDiv.removeClass('success error')
      //           .addClass(type)
      //           .text(message)
      //           .show();
        
      //  setTimeout(function() {
      //    statusDiv.fadeOut();
      //  }, 5000);
      //}
    });
