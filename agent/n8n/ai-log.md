# Задача 1.3. n8n и Ollama: письмо с пояснением модели

Задача: отправлять событие `agent_started` в n8n, чтобы n8n через локальную модель Ollama писал пояснение и отправлял всё это письмом администратору.


## Установка Ollama

Скачал `OllamaSetup.exe` с ollama.com/download, установил. После установки в трее появился значок, команда `ollama` заработала в новом окне cmd.

Скачал модель:
```bat
ollama pull qwen2.5:3b
```
Около 2 ГБ. Проверил:
```bat
ollama run qwen2.5:3b "Объясни по-русски в двух предложениях: агент запустился на компьютере lab-pc, 10 процессоров."
```
Модель ответила что-то вроде «Агент запущен и активно использует 10 процессоров». Тут видна ошибка: 10 процессоров — это сколько их всего у машины, а не сколько занял агент. Модель придумывает правдоподобный текст и не сверяет его с данными. Поэтому в письме всегда отправляю ещё и исходный JSON.

## Установка и запуск n8n

Проверил `node -v`. У меня стоял Node.js 22.x — подходит. (n8n требует версии от 20.19 до 24.x.)

Запуск в отдельном окне cmd:
```bat
npx n8n
```
Первый запуск занял несколько минут — npx скачивал пакет. Когда появилось `Editor is now accessible via: http://localhost:5678`, открыл этот адрес в браузере. Окно терминала не закрывал — иначе n8n останавливается.

При первом входе создал учётную запись владельца. Предложение подключить n8n Assistant пропустил (Set up later in Settings). Нажал Build a workflow и переименовал сценарий в «Агент-сторож: запуск».

## Сценарий из трёх узлов

### Узел Webhook

Добавил узел Webhook. Настройки:
- HTTP Method: POST
- Path: agent
- Respond: Immediately

Запомнил два адреса:
- тестовый: `http://localhost:5678/webhook-test/agent` — работает в редакторе после кнопки Listen for test event;
- боевой: `http://localhost:5678/webhook/agent` — работает после Publish.

### Дописал в brain.py отправку

Добавил `import urllib.request` в начало и функцию в конец файла:

```python
def send(message, url):
    data = json.dumps(message, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=data,
        method="POST",
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.status, response.read().decode("utf-8")


if __name__ == "__main__":
    message = {"event": "agent_started", "os": sys.platform, **get_info()}
    print(json.dumps(message, ensure_ascii=False, indent=2))
    if len(sys.argv) > 1:
        status, body = send(message, sys.argv[1])
        print("n8n:", status, body)
```

Без аргумента `brain.py` печатает JSON как раньше. С аргументом — отправляет на указанный адрес.

Проверка через тестовый адрес: нажал в n8n Listen for test event, затем в терминале:
```bat
cd C:\SysProg\agent\brain
python brain.py http://localhost:5678/webhook-test/agent
```
Вывело:
```json
{
  "event": "agent_started",
  ...
}
n8n: 200 {"message":"Workflow was started"}
```
В окне n8n появились данные — вкладка JSON, поле `body`.

### Узел Basic LLM Chain

Справа от Webhook нажал `+`, выбрал Basic LLM Chain. В настройках:
- Source for Prompt: **Define below**
- Prompt:

```text
Ты помогаешь администратору компьютера. Агент-сторож прислал событие в формате JSON. Объясни по-русски в двух-трёх предложениях, что произошло и нужно ли что-то делать.

Событие: {{ JSON.stringify($json.body) }}
```

Под словом Model нажал `+`, выбрал Ollama Chat Model. В настройках подключения Base URL: `http://localhost:11434` (уже стоял по умолчанию), Save. В поле Model выбрал `qwen2.5:3b` (по умолчанию была llama3.2, её у меня нет).

Нажал Execute step, ответ пришёл секунд через 15. Модель опять написала что-то про «10 ядер», но общий смысл верный.

### Узел Send an Email

Справа от Basic LLM Chain нажал `+`, выбрал Send an Email.

Подключение SMTP. Для проверки использовал Mailpit — локальный SMTP-сервер, который показывает письма в браузере. Скачал бинарник с GitHub, запустил `mailpit.exe`. Интерфейс — http://localhost:8025, SMTP — порт 1025.

Настройки в n8n:
- Host: `localhost`
- Port: `1025`
- SSL/TLS: выключен
- User и Password: пусто

Заполнил письмо:
```text
From Email:    agent@sysprog.local
To Email:      ivan.petrov@example.com
Subject:       Агент-сторож: {{ $('Webhook').item.json.body.event }} на {{ $('Webhook').item.json.body.host }}
Email Format:  Text
Text:
{{ $json.text }}

Сообщение агента:
{{ JSON.stringify($('Webhook').item.json.body, null, 2) }}
```

Тут важный момент: `$json.text` — это ответ модели (из предыдущего узла Basic LLM Chain). А данные агента лежат в узле Webhook, поэтому к ним обращаемся по имени: `$('Webhook').item.json.body`. Если написать `$json.body`, будет пусто.

Execute step — письмо ушло. В Mailpit открылось, внутри ответ модели и JSON с данными.

### Публикация

Нажал Publish вверху справа. Ввёл имя версии, нажал Publish. Статус стал Published.

Запустил с боевым адресом:
```bat
python brain.py http://localhost:5678/webhook/agent
```
В терминале снова `n8n: 200 {"message":"Workflow was started"}`, письмо пришло через 10–20 секунд.

## Проверка через Executions

Открыл вкладку Executions над холстом. В списке:
- зелёная строка без иконки колбы — боевой запуск;
- строки с иконкой колбы — тестовые запуски из редактора.

Клик по строке показывает данные каждого узла. Если письма нет, там видно, какой узел упал (отмечен красным).

## Сохранил в репозиторий

В n8n меню `...` рядом с именем сценария → Download. Скачанный JSON положил в `agent/n8n/agent-started.json`.

Проверил: внутри только имена подключений (Ollama account, SMTP account), паролей нет. Закоммитил.

```bat
git add agent\brain\brain.py agent\n8n\agent-started.json
git commit -m "brain: отправка agent_started в n8n, сценарий с пояснением модели"
```

## Какие были проблемы

1. Ловил `n8n: 404`, когда забывал нажать Listen for test event или путал адреса: `/webhook-test/agent` и `/webhook/agent`. На тестовой фазе нужен именно `-test`.
2. Один раз модель отвечала по-английски. Оказалось, в узле Ollama Chat Model по умолчанию стояла llama3.2. Выбрал вручную qwen2.5:3b.

## Итог

Агент отправляет событие в n8n, локальная модель пишет пояснение, письмо приходит без участия человека. В письме есть и пояснение модели, и исходный JSON — по нему администратор сверит цифры, если модель ошиблась.
