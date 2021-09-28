/*
PZEM EDL - PZEM Event Driven Library

This code implements communication and data exchange with PZEM004T V3.0 module using MODBUS proto
and provides an API for energy metrics monitoring and data processing.

This file is part of the 'PZEM event-driven library' project.

Copyright (C) Emil Muratov, 2021
GitHub: https://github.com/vortigont/pzem-edl
*/

#include "uartq.hpp"

UartQ::~UartQ(){
    stopQueues();
    uart_driver_delete(port);
    vQueueDelete(rx_evt_queue);
    rx_evt_queue = nullptr;
    rx_callback = nullptr;
}

void UartQ::init(const uart_config_t &uartcfg, int gpio_rx, int gpio_tx){
    // TODO: catch port init errors
    uart_param_config(port, &uartcfg);
    uart_set_pin(port, gpio_tx, gpio_rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(port, RX_BUF_SIZE, TX_BUF_SIZE, rx_evt_queue_DEPTH, &rx_evt_queue, 0);
    rts_sem = xSemaphoreCreateBinary();     // Ready-To-Send-next semaphore
}


void UartQ::stopQueues(){
    stop_TX_msg_queue();
    stop_RX_evt_queue();
}

void UartQ::stop_TX_msg_queue(){
    if (t_txq){
        vTaskDelete(t_txq);
        t_txq = nullptr;
    }

    if (!tx_msg_q)
        return;

    QueueHandle_t _t = tx_msg_q;
    tx_msg_q = nullptr;

    // очищаем все сообщения из очереди
    TX_msg* msg = nullptr;
    while (xQueueReceive(_t, &(msg), (portTickType)0) == pdPASS ){
        delete msg;
    }

    vQueueDelete(_t);
}

void UartQ::stop_RX_evt_queue(){
    if (t_rxq){
        vTaskDelete(t_rxq);
        t_rxq = nullptr;
    }
}

bool UartQ::txenqueue(TX_msg *msg){
    if (!msg)
        return false;

    // check if q is present
    if (!tx_msg_q){
        delete msg;     // пакет надо удалять сразу, иначе, не попав в очередь, он останется потерян в памяти
        return false;
    }

    #ifdef PZEM_EDL_DEBUG
        ESP_LOGD(TAG, "TX packet enque, t: %ld", esp_timer_get_time()/1000);
    #endif

    if (xQueueSendToBack(tx_msg_q, (void *) &msg, (TickType_t)0) == pdTRUE)
        return true;
    else {
        delete msg;     // пакет надо удалять сразу, иначе, не попав в очередь, он останется потерян в памяти
        return false;
    }
}

void UartQ::attach_RX_hndlr(datahandler_t f){
    if (!f)
        return;
    rx_callback = std::move(f);
    start_RX_evt_queue();
}

void UartQ::detach_RX_hndlr(){
    rx_callback = nullptr;
    stop_RX_evt_queue();
}

//#define PZEM_EDL_DEBUG
#ifdef PZEM_EDL_DEBUG
void UartQ::rx_msg_debug(const RX_msg *m){
    if (!m->len){
        ESP_LOGE(TAG, "Zero len RX packet");
        return;
    }
    char *buff = new char[m->len * 4];
    char *ptr = buff;
    for(uint8_t i=0; i < m->len; ++i){
        ptr += sprintf(ptr, "%.2x ", m->rawdata[i]);
    }
    ptr=0;
    // выводим с ERROR severity, т.к. по умолчанию CORE_DEBUG_LEVEL глушит дебаг
    ESP_LOGE(TAG, "RX packet, len:%d, CRC: %s, HEX: %s", m->len, m->valid ? "OK":"BAD", buff);
    delete[] buff;
}

void UartQ::tx_msg_debug(const TX_msg *m){
    if (!m->len){
        ESP_LOGE(TAG, "Zero len TX packet");
        return;
    }
    char *buff = new char[m->len * 4];
    char *ptr = buff;
    for(uint8_t i=0; i < m->len; ++i){
        ptr += sprintf(ptr, "%.2x ", m->data[i]);
    }
    ptr=0;
    // print with ERROR severity, so no need to redefine CORE_DEBUG_LEVEL
    ESP_LOGE(TAG, "TX packet, len:%d, HEX: %s", m->len, buff);
    delete[] buff;
}

#else
void UartQ::rx_msg_debug(RX_msg *m){}
void UartQ::rx_msg_debug(TX_msg *m){}
#endif

const char* PZPort::getDescr() const {
    return descr.get();
}

bool PZPort::active(bool newstate){
    if (newstate)
        qrun = startQueues();
    else { stopQueues(); qrun = false;}

    return qrun;
}
