// Copyright (C) 2013 July IGHOR.
// I want to create trading application that can be configured for any rule and strategy.
// If you want to help me please Donate: 1d6iMwjjNo8ZGYeJBZKXgcgVk9o7fXcjc
// For any questions please use contact form https://sourceforge.net/projects/bitcointrader/
// Or send e-mail directly to julyighor@gmail.com
//
// You may use, distribute and copy the Qt Bitcion Trader under the terms of
// GNU General Public License version 3

#include "exchange_bitstamp.h"
#include <openssl/hmac.h>
#include "main.h"
#include <QDateTime>

Exchange_Bitstamp::Exchange_Bitstamp(QByteArray pRestSign, QByteArray pRestKey)
	: Exchange()
{
	exchangeID="Bitstamp";
	depthAsks=0;
	depthBids=0;
	forceDepthLoad=false;
	julyHttp=0;
	tickerOnly=false;
	privateRestSign=pRestSign;
	privateRestKey=pRestKey.split(':').last();
	privateClientId=pRestKey.split(':').first();
	moveToThread(this);
	authRequestTime.restart();
	privateNonce=(QDateTime::currentDateTime().toTime_t()-1371854884)*10;
}

Exchange_Bitstamp::~Exchange_Bitstamp()
{
}

void Exchange_Bitstamp::clearVariables()
{
	isFirstTicker=true;
	cancelingOrderIDs.clear();
	lastTickerHigh=0.0;
	lastTickerLow=0.0;
	lastTickerSell=0.0;
	lastTickerLast=0.0;
	lastTickerBuy=0.0;
	lastTickerVolume=0.0;
	lastBtcBalance=0.0;
	lastUsdBalance=0.0;
	lastVolume=0.0;
	lastFee=0.0;
	secondPart=0;
	apiDownCounter=0;
	lastHistory.clear();
	lastOrders.clear();
	reloadDepth();
	lastInfoReceived=false;
	lastBidAskTimestamp.clear();
	lastFetchDate=QByteArray::number(QDateTime::currentDateTime().addSecs(-600).toTime_t())+"0000000";
}

void Exchange_Bitstamp::clearValues()
{
	clearVariables();
	if(julyHttp)julyHttp->clearPendingData();
}

void Exchange_Bitstamp::secondSlot()
{
	static int infoCounter=0;
	switch(infoCounter)
	{
	case 0: if(!tickerOnly&&!isReplayPending(204))sendToApi(204,"open_orders/",true,true); break;
	case 1: if(!isReplayPending(202))sendToApi(202,"balance/",true,true); break;
	case 2: if(!isReplayPending(103))sendToApi(103,"ticker/",false,true); break;
	case 3: if(!isReplayPending(109))sendToApi(109,"transactions/",false,true); break;
	case 4: if(lastHistory.isEmpty()&&!isReplayPending(208))sendToApi(208,"user_transactions/",true,true); break;
	default: break;
	}

	if(!depthRefreshBlocked&&(forceDepthLoad||infoCounter==5&&!isReplayPending(111)))
	{
		emit depthRequested();
		sendToApi(111,"order_book/",false,true);
		forceDepthLoad=false;
	}

	if(++infoCounter>5)
	{
		infoCounter=0;
		quint32 syncNonce=(QDateTime::currentDateTime().toTime_t()-1371854884)*10;
		if(privateNonce<syncNonce)privateNonce=syncNonce;
	}

	Exchange::secondSlot();
}

bool Exchange_Bitstamp::isReplayPending(int reqType)
{
	if(julyHttp==0)return false;
	return julyHttp->isReqTypePending(reqType);
}

void Exchange_Bitstamp::getHistory(bool force)
{
	if(tickerOnly)return;
	if(force)lastHistory.clear();
	if(!isReplayPending(208))sendToApi(208,"user_transactions/",true,true);
}

void Exchange_Bitstamp::buy(double apiBtcToBuy, double apiPriceToBuy)
{
	if(tickerOnly)return;
	QByteArray params="amount="+QByteArray::number(apiBtcToBuy,'f',btcDecimals)+"&price="+QByteArray::number(apiPriceToBuy,'f',priceDecimals);
	if(debugLevel)logThread->writeLog("Buy: "+params,2);
	sendToApi(306,"buy/",true,true,params);
}

void Exchange_Bitstamp::sell(double apiBtcToSell, double apiPriceToSell)
{
	if(tickerOnly)return;
	QByteArray params="amount="+QByteArray::number(apiBtcToSell,'f',btcDecimals)+"&price="+QByteArray::number(apiPriceToSell,'f',priceDecimals);
	if(debugLevel)logThread->writeLog("Sell: "+params,2);
	sendToApi(307,"sell/",true,true,params);
}

void Exchange_Bitstamp::cancelOrder(QByteArray order)
{
	if(tickerOnly)return;
	cancelingOrderIDs<<order;
	if(debugLevel)logThread->writeLog("Cancel order: "+order,2);
	sendToApi(305,"cancel_order/",true,true,"id="+order);
}

void Exchange_Bitstamp::sendToApi(int reqType, QByteArray method, bool auth, bool sendNow, QByteArray commands)
{
	if(julyHttp==0)
	{ 
		julyHttp=new JulyHttp("www.bitstamp.net","",this);
		connect(julyHttp,SIGNAL(anyDataReceived()),mainWindow_,SLOT(anyDataReceived()));
		connect(julyHttp,SIGNAL(setDataPending(bool)),mainWindow_,SLOT(setDataPending(bool)));
		connect(julyHttp,SIGNAL(apiDown(bool)),mainWindow_,SLOT(setApiDown(bool)));
		connect(julyHttp,SIGNAL(errorSignal(QString)),mainWindow_,SLOT(showErrorMessage(QString)));
		connect(julyHttp,SIGNAL(sslErrorSignal(const QList<QSslError> &)),this,SLOT(sslErrors(const QList<QSslError> &)));
		connect(julyHttp,SIGNAL(dataReceived(QByteArray,int)),this,SLOT(dataReceivedAuth(QByteArray,int)));
	}

	if(auth)
	{
		QByteArray postData=QByteArray::number(++privateNonce);
		postData="key="+privateRestKey+"&signature="+hmacSha256(privateRestSign,QByteArray(postData+privateClientId+privateRestKey)).toHex().toUpper()+"&nonce="+postData;
		if(!commands.isEmpty())postData.append("&"+commands);
		if(sendNow)
		julyHttp->sendData(reqType, "POST /api/"+method,postData);
		else
		julyHttp->prepareData(reqType, "POST /api/"+method, postData);

	}
	else
	{
		if(sendNow)
			julyHttp->sendData(reqType, "GET /api/"+method);
		else 
			julyHttp->prepareData(reqType, "GET /api/"+method);
	}
}

void Exchange_Bitstamp::depthUpdateOrder(double price, double amount, bool isAsk)
{
	if(isAsk)
	{
		if(depthAsks==0)return;
		DepthItem newItem;
		newItem.price=price;
		newItem.volume=amount;
		if(newItem.isValid())
			(*depthAsks)<<newItem;
	}
	else
	{
		if(depthBids==0)return;
		DepthItem newItem;
		newItem.price=price;
		newItem.volume=amount;
		if(newItem.isValid())
			(*depthBids)<<newItem;
	}
}

void Exchange_Bitstamp::depthSubmitOrder(QMap<double,double> *currentMap ,double priceDouble, double amount, bool isAsk)
{
	if(priceDouble==0.0||amount==0.0)return;

	if(isAsk)
	{
		(*currentMap)[priceDouble]=amount;
		if(lastDepthAsksMap.value(priceDouble,0.0)!=amount)
			depthUpdateOrder(priceDouble,amount,true);
	}
	else
	{
		(*currentMap)[priceDouble]=amount;
		if(lastDepthBidsMap.value(priceDouble,0.0)!=amount)
			depthUpdateOrder(priceDouble,amount,false);
	}
}

void Exchange_Bitstamp::reloadDepth()
{
	lastDepthBidsMap.clear();
	lastDepthAsksMap.clear();
	lastDepthData.clear();
	Exchange::reloadDepth();
}

void Exchange_Bitstamp::dataReceivedAuth(QByteArray data, int reqType)
{
	bool success=!data.startsWith("{\"error\"")&&(data.startsWith("{")||data.startsWith("["))||data=="true"||data=="false";
	switch(reqType)
	{
	case 103: //ticker
		if(!success)break;
			if(data.startsWith("{\"high\":"))
			{
				QByteArray tickerTimestamp=getMidData("\"timestamp\": \"","\"",&data);
				QByteArray tickerHigh=getMidData("\"high\": \"","\"",&data);
				if(!tickerHigh.isEmpty())
				{
					double newTickerHigh=tickerHigh.toDouble();
					if(newTickerHigh!=lastTickerHigh)emit tickerHighChanged(newTickerHigh);
					lastTickerHigh=newTickerHigh;
				}

				QByteArray tickerLow=getMidData("\"low\": \"","\"",&data);
				if(!tickerLow.isEmpty())
				{
					double newTickerLow=tickerLow.toDouble();
					if(newTickerLow!=lastTickerLow)emit tickerLowChanged(newTickerLow);
					lastTickerLow=newTickerLow;
				}

				QByteArray tickerVolume=getMidData("\"volume\": \"","\"",&data);
				if(!tickerVolume.isEmpty())
				{
					double newTickerVolume=tickerVolume.toDouble();
					if(newTickerVolume!=lastTickerVolume)emit tickerVolumeChanged(newTickerVolume);
					lastTickerVolume=newTickerVolume;
				}

				QByteArray tickerLast=getMidData("\"last\": \"","\"",&data);
				if(!tickerLast.isEmpty())
				{
					double newTickerLast=tickerLast.toDouble();
					if(newTickerLast!=lastTickerLast)emit tickerLastChanged(newTickerLast);
					lastTickerLast=newTickerLast;
				}
				if(tickerTimestamp>lastBidAskTimestamp)
				{
					QByteArray tickerSell=getMidData("\"bid\": \"","\"",&data);
					if(!tickerSell.isEmpty())
					{
						double newTickerSell=tickerSell.toDouble();
						if(newTickerSell!=lastTickerSell)emit tickerSellChanged(newTickerSell);
						lastTickerSell=newTickerSell;
					}

					QByteArray tickerBuy=getMidData("\"ask\": \"","\"",&data);
					if(!tickerBuy.isEmpty())
					{
						double newTickerBuy=tickerBuy.toDouble();
						if(newTickerBuy!=lastTickerBuy)emit tickerBuyChanged(newTickerBuy);
						lastTickerBuy=newTickerBuy;
					}
					lastBidAskTimestamp=tickerTimestamp;
				}

				if(isFirstTicker)
				{
					emit firstTicker();
					isFirstTicker=false;
				}
			}
			else if(debugLevel)logThread->writeLog("Invalid ticker data:"+data,2);
		break;//ticker
	case 109: //api/transactions
		if(success&&data.size()>32)
		{
			if(data.startsWith("[{\"date\":"))
			{
				QStringList tradeList=QString(data).split("}, {");
				for(int n=0;n<tradeList.count();n++)
				{
					QByteArray tradeData=tradeList.at(n).toAscii();
					QByteArray tradeDate=getMidData("\"date\": \"","\"",&tradeData);
					QByteArray nextFetchDate=tradeDate+getMidData("\"tid\": ",",",&tradeData);
					if(nextFetchDate<=lastFetchDate)continue;
					double doubleAmount=getMidData("\"amount\": \"","\"",&tradeData).toDouble();
					double doublePrice=getMidData("\"price\": \"","\"",&tradeData).toDouble();
					if(doubleAmount>0.0&&doublePrice>0.0)
					{
						emit addLastTrade(doubleAmount,tradeDate.toLongLong(),doublePrice,currencySymbol,true);
						if(n==tradeList.count()-1&&!nextFetchDate.isEmpty())lastFetchDate=nextFetchDate;
					}
					else if(debugLevel)logThread->writeLog("Invalid trades fetch data line:"+tradeData,2);
				}
			}
			else if(debugLevel)logThread->writeLog("Invalid trades fetch data:"+data,2);
		}
		break;
	case 111: //api/order_book
		if(data.startsWith("{\"timestamp\":"))
		{
			emit depthRequestReceived();

			if(lastDepthData!=data)
			{
				lastDepthData=data;
				depthAsks=new QList<DepthItem>;
				depthBids=new QList<DepthItem>;

				QByteArray tickerTimestamp=getMidData("\"timestamp\": \"","\"",&data);
				QMap<double,double> currentAsksMap;
				QStringList asksList=QString(getMidData("\"asks\": [[","]]",&data)).split("], [");
				double groupedPrice=0.0;
				double groupedVolume=0.0;
				int rowCounter=0;
				bool updateTicker=tickerTimestamp>lastBidAskTimestamp;
				if(updateTicker)lastBidAskTimestamp=tickerTimestamp;

				for(int n=0;n<asksList.count();n++)
				{
					if(depthCountLimit&&rowCounter>=depthCountLimit)break;
					QByteArray currentRow=asksList.at(n).toAscii();
					double priceDouble=getMidData("\"","\"",&currentRow).toDouble();
					double amount=getMidData(", \"","\"",&currentRow).toDouble();

					if(n==0&&updateTicker)emit tickerBuyChanged(priceDouble);

					if(groupPriceValue>0.0)
					{
						if(n==0)
						{
							emit depthFirstOrder(priceDouble,amount,true);
							groupedPrice=groupPriceValue*(int)(priceDouble/groupPriceValue);
							groupedVolume=amount;
						}
						else
						{
							bool matchCurrentGroup=priceDouble<groupedPrice+groupPriceValue;
							if(matchCurrentGroup)groupedVolume+=amount;
							if(!matchCurrentGroup||n==asksList.count()-1)
							{
								depthSubmitOrder(&currentAsksMap,groupedPrice+groupPriceValue,groupedVolume,true);
								rowCounter++;
								groupedVolume=amount;
								groupedPrice+=groupPriceValue;
							}
						}
					}
					else
					{
					depthSubmitOrder(&currentAsksMap,priceDouble,amount,true);
					rowCounter++;
					}
				}
				QList<double> currentAsksList=lastDepthAsksMap.keys();
				for(int n=0;n<currentAsksList.count();n++)
					if(currentAsksMap.value(currentAsksList.at(n),0)==0)depthUpdateOrder(currentAsksList.at(n),0.0,true);//Remove price
				lastDepthAsksMap=currentAsksMap;

				QMap<double,double> currentBidsMap;
				QStringList bidsList=QString(getMidData("\"bids\": [[","]]",&data)).split("], [");
				groupedPrice=0.0;
				groupedVolume=0.0;
				rowCounter=0;
				for(int n=0;n<bidsList.count();n++)
				{
					if(depthCountLimit&&rowCounter>=depthCountLimit)break;
					QByteArray currentRow=bidsList.at(n).toAscii();
					double priceDouble=getMidData("\"","\"",&currentRow).toDouble();
					double amount=getMidData(", \"","\"",&currentRow).toDouble();

					if(n==0&&updateTicker)emit tickerSellChanged(priceDouble);

					if(groupPriceValue>0.0)
					{
						if(n==0)
						{
							emit depthFirstOrder(priceDouble,amount,false);
							groupedPrice=groupPriceValue*(int)(priceDouble/groupPriceValue);
							groupedVolume=amount;
						}
						else
						{
							bool matchCurrentGroup=priceDouble>groupedPrice-groupPriceValue;
							if(matchCurrentGroup)groupedVolume+=amount;
							if(!matchCurrentGroup||n==bidsList.count()-1)
							{
								depthSubmitOrder(&currentBidsMap,groupedPrice-groupPriceValue,groupedVolume,false);
								rowCounter++;
								groupedVolume=amount;
								groupedPrice-=groupPriceValue;
							}
						}
					}
					else
					{
						depthSubmitOrder(&currentBidsMap,priceDouble,amount,false);
						rowCounter++;
					}
				}
				QList<double> currentBidsList=lastDepthBidsMap.keys();
				for(int n=0;n<currentBidsList.count();n++)
					if(currentBidsMap.value(currentBidsList.at(n),0)==0)depthUpdateOrder(currentBidsList.at(n),0.0,false);//Remove price
				lastDepthBidsMap=currentBidsMap;

				emit depthSubmitOrders(depthAsks, depthBids);
				depthAsks=0;
				depthBids=0;
			}
		}
		else if(debugLevel)logThread->writeLog("Invalid depth data:"+data,2);
		break;
	case 202: //balance
		{
			if(!success)break;
			if(data.startsWith("{\"btc_reserved\""))
			{
				lastInfoReceived=true;
				if(debugLevel)logThread->writeLog("Info: "+data);

				double accFee=getMidData("\"fee\": \"","\"",&data).toDouble();
				if(accFee>0.0)emit accFeeChanged(accFee);

				QByteArray btcBalance=getMidData("\"btc_balance\": \"","\"",&data);
				if(!btcBalance.isEmpty())
				{
					double newBtcBalance=btcBalance.toDouble();
					if(lastBtcBalance!=newBtcBalance)emit accBtcBalanceChanged(newBtcBalance);
					lastBtcBalance=newBtcBalance;
				}

				QByteArray usdBalance=getMidData("\"usd_balance\": \"","\"",&data);
				if(!usdBalance.isEmpty())
				{
					double newUsdBalance=usdBalance.toDouble();
					if(newUsdBalance!=lastUsdBalance)emit accUsdBalanceChanged(newUsdBalance);
					lastUsdBalance=newUsdBalance;
				}
			}
			else if(debugLevel)logThread->writeLog("Invalid Info data:"+data,2);
		}
		break;//balance
	case 204://open_orders
		if(!success)break;
		if(data=="[]"){lastOrders.clear();emit ordersIsEmpty();break;}
		if(data.startsWith("[")&&data.endsWith("]"))
		{
			if(lastOrders!=data)
			{
				lastOrders=data;
				if(data.size()>3)
				{
				data.remove(0,2);
				data.remove(data.size()-2,2);
				}
				QStringList ordersList=QString(data).split("}, {");
				QList<OrderItem> *orders=new QList<OrderItem>;
				for(int n=0;n<ordersList.count();n++)
				{	
					OrderItem currentOrder;
					QByteArray currentOrderData=ordersList.at(n).toAscii();
					currentOrder.oid=getMidData("\"id\": ",",",&currentOrderData);

					QByteArray dateTimeData=getMidData("\"datetime\": \"","\"",&currentOrderData);
					QDateTime orderDateTime=QDateTime::fromString(dateTimeData,"yyyy-MM-dd HH:mm:ss");
					orderDateTime.setTimeSpec(Qt::UTC);
					currentOrder.date=orderDateTime.toTime_t();
					currentOrder.type=getMidData("\"type\": ",",",&currentOrderData)=="1";
					currentOrder.status=1;
					currentOrder.amount=getMidData("\"amount\": \"","\"",&currentOrderData).toDouble();
					currentOrder.price=getMidData("\"price\": \"","\"",&currentOrderData).toDouble();
					currentOrder.symbol=currencySymbol;
					if(currentOrder.isValid())(*orders)<<currentOrder;
				}
				emit ordersChanged(orders);
				lastInfoReceived=false;
			}
		}
		else if(debugLevel)logThread->writeLog("Invalid Orders data:"+data,2);
		break;//open_orders
	case 305: //cancel_order
		{
			if(!success)break;
			if(!cancelingOrderIDs.isEmpty())
			{
			if(data=="true")emit orderCanceled(cancelingOrderIDs.first());
			if(debugLevel)logThread->writeLog("Order canceled:"+cancelingOrderIDs.first(),2);
			cancelingOrderIDs.removeFirst();
			}
		}
		break;//cancel_order
	case 306: //order/buy
		if(!success||!debugLevel)break;
			  if(data.startsWith("{\"result\":\"success\",\"data\":\""))logThread->writeLog("Buy OK: "+data);
			  else logThread->writeLog("Invalid Order Buy Data:"+data);
		break;//order/buy
	case 307: //order/sell
		if(!success||!debugLevel)break;
			  if(data.startsWith("{\"result\":\"success\",\"data\":\""))logThread->writeLog("Sell OK: "+data);
			  else logThread->writeLog("Invalid Order Sell Data:"+data);
		break;//order/sell
	case 208: //user_transactions
		if(!success)break;
		if(data.startsWith("["))
		{
			if(lastHistory!=data)
			{
				lastHistory=data;
				if(data=="[]")break;

				QList<HistoryItem> *historyItems=new QList<HistoryItem>;
				QString newLog(data);
				QStringList dataList=newLog.split("}, {");
				newLog.clear();
				for(int n=0;n<dataList.count();n++)
				{
					HistoryItem currentHistoryItem;
					currentHistoryItem.type=0;
					currentHistoryItem.price=0.0;
					currentHistoryItem.volume=0.0;
					currentHistoryItem.date=0;

					QByteArray curLog(dataList.at(n).toAscii());
					int logTypeInt=getMidData("\"type\": ",",",&curLog).toInt();
					QByteArray btcAmount=getMidData("\"btc\": \"","\"",&curLog);
					bool negativeAmount=btcAmount.startsWith("-");
					if(negativeAmount)btcAmount.remove(0,1);
					QDateTime orderDateTime=QDateTime::fromString(getMidData("\"datetime\": \"","\"",&curLog),"yyyy-MM-dd HH:mm:ss");
					orderDateTime.setTimeSpec(Qt::UTC);

					currentHistoryItem.price=getMidData("_usd\": \"","\"",&curLog).toDouble();
					currentHistoryItem.volume=btcAmount.toDouble();

					QByteArray logType;

					if(logTypeInt==0)currentHistoryItem.type=4;//Deposit
					else
					if(logTypeInt==1)currentHistoryItem.type=5;//Withdrawal
					else
					if(logTypeInt==2)//Market Trade
					{
						if(negativeAmount)currentHistoryItem.type=1;//Sell
						else currentHistoryItem.type=2;//Buy
					}
					currentHistoryItem.date=orderDateTime.toTime_t();
					currentHistoryItem.symbol="BTCUSD";
					if(currentHistoryItem.isValid())(*historyItems)<<currentHistoryItem;
				}
				emit historyChanged(historyItems);
			}
		}
		else if(debugLevel)logThread->writeLog("Invalid History data:"+data.left(200),2);
		break;//user_transactions
	default: break;
	}

	static int errorCount=0;
	if(!success&&reqType!=305)
	{
		errorCount++;
		QString errorString;
		bool invalidMessage=!data.startsWith("{");
		if(!invalidMessage)
		{
			errorString=getMidData("[\"","\"]",&data);
			if(errorString.isEmpty())
			{
				QByteArray nErrorString=getMidData("{\"error\":","}",&data);
				errorString=getMidData("\"","\"",&nErrorString);
			}
		}
		else errorString=data;
		if(debugLevel)logThread->writeLog("API Error: "+errorString.toAscii()+" ReqType:"+QByteArray::number(reqType),2);
		if(errorCount<3&&reqType<300&&errorString!="Invalid username and/or password")return;
		if(errorString.isEmpty())return;
		errorString.append("<br>"+QString::number(reqType));
		if(invalidMessage||reqType<300)emit showErrorMessage("I:>"+errorString);
	}
	else errorCount=0;
}

void Exchange_Bitstamp::sslErrors(const QList<QSslError> &errors)
{
	QStringList errorList;
	for(int n=0;n<errors.count();n++)errorList<<errors.at(n).errorString();
	if(debugLevel)logThread->writeLog(errorList.join(" ").toAscii(),2);
	emit showErrorMessage("SSL Error: "+errorList.join(" "));
}