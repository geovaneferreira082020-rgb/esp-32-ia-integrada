este é um  dos meus primeiros projetos com um cyberdesk com ia integrada, ele utiliza apenas um teclado, um modulo leitor de memoria microSD SPI, tela touchscreen 2.4" SPI, um memoria microSD da escolha e uma esp32 devkit v1
para utilizar ele, deve se usar algumas bibliotecas do arduino, sendo elas:
<TFT_eSPI.h>
<ArduinoJson.h>
<NimBLEDevice.h>
o projeto usa a inteligencia artificial do gemini 2.8 flash para o projeto, usando apenas 2082 tokens
para configurar o esp32 para responder certinho, você deve utilizar o user setup também, para conectar os gpio que serão utilizados
