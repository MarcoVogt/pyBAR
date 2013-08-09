#include "Interpret.h"

Interpret::Interpret(void)
{
  setSourceFileName("Interpret");
  _NbCID = 16;
  _maxTot = 14;
  _fEI4B = false;
  _metaDataSet = false;
  _debugEvents = false;
  _lastMetaIndexNotSet = 0;
  _lastWordIndexSet = 0;
  _metaEventIndexLength = 0;
  _metaEventIndex = 0;
  allocateHitInfoArray();
  allocateHitBufferArray();
  allocateTriggerErrorCounterArray();
  allocateErrorCounterArray();
  allocateServiceRecordCounterArray();
  resetCounters();
}

Interpret::~Interpret(void)
{
  deleteMetaEventIndexArray();
  deleteHitInfoArray();
  deleteHitBufferArray();
  deleteTriggerErrorCounterArray();
  deleteErrorCounterArray();
  deleteServiceRecordCounterArray();
}

bool Interpret::interpretRawData(unsigned int* pDataWords, const unsigned int& pNdataWords)
{
  if(Basis::debugSet()){
    std::stringstream tDebug;
    tDebug<<"interpretRawData with "<<pNdataWords<<" words";
    debug(tDebug.str());
  }
  _hitIndex = 0;

	//temporary variables set according to the actual SRAM word
	unsigned int tActualLVL1ID = 0;							//LVL1ID of the actual data header
	unsigned int tActualBCID = 0;								//BCID of the actual data header
  unsigned int tActualSRcode = 0;							//Service record code of the actual service record
  unsigned int tActualSRcounter = 0;					//Service record counter value of the actual service record
	int tActualCol1 = 0;												//column position of the first hit in the actual data record
	int tActualRow1 = 0;												//row position of the first hit in the actual data record
	int tActualTot1 = -1;												//tot value of the first hit in the actual data record
	int tActualCol2 = 0;												//column position of the second hit in the actual data record
	int tActualRow2 = 0;												//row position of the second hit in the actual data record
	int tActualTot2 = -1;												//tot value of the second hit in the actual data record

  int counter = 0;

	for (unsigned int iWord = 0; iWord < pNdataWords; ++iWord){	//loop over the SRAM words
    if(_debugEvents){
      if(_nEvents >= _startDebugEvent && _nEvents <= _stopDebugEvent)
        setDebugOutput();
      else
        setDebugOutput(false);
    }

    correlateMetaWordIndex(_nEvents, _nDataWords);
    _nDataWords++;
		unsigned int tActualWord = pDataWords[iWord];			//take the actual SRAM word
		tActualTot1 = -1;												          //TOT1 value stays negative if it can not be set properly in getHitsfromDataRecord()
		tActualTot2 = -1;												          //TOT2 value stays negative if it can not be set properly in getHitsfromDataRecord()
		if (getTimefromDataHeader(tActualWord, tActualLVL1ID, tActualBCID)){	//data word is data header if true is returned
			_nDataHeaders++;
      if (tNdataHeader > _NbCID-1){	                  //maximum event window is reached (tNdataHeader > BCIDs, mostly tNdataHeader > 15), so create new event
        if(tNdataRecord==0)
          _nEmptyEvents++;
        addEvent();
			}
			if (tNdataHeader == 0){								          //set the BCID of the first data header
				tStartBCID = tActualBCID;
				tStartLVL1ID = tActualLVL1ID;
			}
			else{
				tDbCID++;										                  //increase relative BCID counter [0:15]
				if(_fEI4B){
					if(tStartBCID + tDbCID > __BCIDCOUNTERSIZE_FEI4B-1)	//BCID counter overflow for FEI4B (10 bit BCID counter)
						tStartBCID = tStartBCID - __BCIDCOUNTERSIZE_FEI4B;
        }
				else{
					if(tStartBCID + tDbCID > __BCIDCOUNTERSIZE_FEI4A-1)	//BCID counter overflow for FEI4A (8 bit BCID counter)
						tStartBCID = tStartBCID - __BCIDCOUNTERSIZE_FEI4A;
        }

				if(tStartBCID+tDbCID != tActualBCID){  //check if BCID is increasing by 1s in the event window, if not close actual event and create new event with actual data header
          if(_firstTriggerNrSet && tActualLVL1ID == tStartLVL1ID) //happens sometimes, non inc. BCID, FE feature, only abort if no external trigger is used or the LVL1ID is not constant
            addEventErrorCode(__BCID_JUMP);
          else{
					  tBCIDerror = true;					       //BCID number wrong, abort event and take actual data header for the first hit of the new event
            addEventErrorCode(__EVENT_INCOMPLETE);
          }
        }
        if (!tBCIDerror && tActualLVL1ID != tStartLVL1ID){    //LVL1ID not constant, is expected for CMOS pulse trigger/hit OR, but not for trigger word triggering
					tLVL1IDisConst = false;
          addEventErrorCode(__NON_CONST_LVL1ID);
        }
			}
      tNdataHeader++;										       //increase data header counter
      if (Basis::debugSet())
        debug(std::string(" ")+IntToStr(_nDataWords)+" DH LVL1ID/BCID "+IntToStr(tActualLVL1ID)+"/"+IntToStr(tActualBCID)+"\t"+IntToStr(_nEvents));
		}
		else if (isDataRecord(tActualWord)){	//data word is data record if true is returned
			if (getHitsfromDataRecord(tActualWord, tActualCol1, tActualRow1, tActualTot1, tActualCol2, tActualRow2, tActualTot2)){
        tNdataRecord++;										  //increase data record counter for this event
			  _nDataRecords++;									  //increase total data record counter
			  if(tActualTot1 >= 0)								//add hit if hit info is reasonable (TOT1 >= 0)
          addHit(tDbCID, tActualLVL1ID, tActualCol1, tActualRow1, tActualTot1, tActualBCID);
			  if(tActualTot2 >= 0)								//add hit if hit info is reasonable and set (TOT2 >= 0)
          addHit(tDbCID, tActualLVL1ID, tActualCol2, tActualRow2, tActualTot2, tActualBCID);
        if (Basis::debugSet()) 
          debug(std::string(" ")+IntToStr(_nDataWords)+" DR COL1/ROW1/TOT1  COL2/ROW2/TOT2 "+IntToStr(tActualCol1)+"/"+IntToStr(tActualRow1)+"/"+IntToStr(tActualTot1)+"  "+IntToStr(tActualCol2)+"/"+IntToStr(tActualRow2)+"/"+IntToStr(tActualTot2)+" rBCID "+IntToStr(tDbCID)+"\t"+IntToStr(_nEvents));
      }		
    }
    else if (isTriggerWord(tActualWord)){ //data word is trigger word, is first word of the event data if external trigger is present
			_nTriggers++;										    //increase the total trigger number counter
      if (tNdataHeader > _NbCID-1){	      //special case: first word is trigger word
        if(tNdataRecord==0)
          _nEmptyEvents++;
        addEvent();
			}
      tTriggerWord++;                     //trigger event counter increase
			tTriggerNumber = TRIGGER_NUMBER_MACRO_NEW(tActualWord); //actual trigger number
      if (Basis::debugSet())
        debug(std::string(" ")+IntToStr(_nDataWords)+" TR NUMBER "+IntToStr(tTriggerNumber));

      //TLU error handling
      if(!_firstTriggerNrSet)
        _firstTriggerNrSet = true;
      else if(_lastTriggerNumber + 1 != tTriggerNumber && !(_lastTriggerNumber == __MAXTLUTRGNUMBER && tTriggerNumber == 0)){
        addTriggerErrorCode(__TRG_NUMBER_INC_ERROR);
        if (Basis::warningSet())
          warning("interpretRawData: Trigger Number not increasing by 1 (old/new): "+IntToStr(_lastTriggerNumber)+"/"+IntToStr(tTriggerNumber));
        if (Basis::debugSet())
          printInterpretedWords(pDataWords, pNdataWords, iWord-10, iWord+250);
      }

      if ((tTriggerNumber & TRIGGER_ERROR_TRG_ACCEPT) == TRIGGER_ERROR_TRG_ACCEPT){
        addTriggerErrorCode(__TRG_ERROR_TRG_ACCEPT);
        if(Basis::warningSet())
          warning(std::string("interpretRawData: TRIGGER_ERROR_TRG_ACCEPT"));
      }
      if ((tTriggerNumber & TRIGGER_ERROR_LOW_TIMEOUT) == TRIGGER_ERROR_LOW_TIMEOUT){
        addTriggerErrorCode(__TRG_ERROR_LOW_TIMEOUT);
        if(Basis::warningSet())
          warning(std::string("interpretRawData: TRIGGER_ERROR_LOW_TIMEOUT"));
      }
      _lastTriggerNumber = tTriggerNumber;
		}
    else if (getInfoFromServiceRecord(tActualWord, tActualSRcode, tActualSRcounter)){ //data word is service record
        info(IntToStr(_nDataWords)+" SR "+IntToStr(tActualSRcode));
        addServiceRecord(tActualSRcode);
        addEventErrorCode(__HAS_SR);
        _nServiceRecords++;
		}
		else{
			if (!isOtherWord(tActualWord)){			//other for hit interpreting uninteressting data, else data word unknown
        addEventErrorCode(__UNKNOWN_WORD);
        _nUnknownWords++;
        if(Basis::warningSet())
				  warning("interpretRawData: "+IntToStr(_nDataWords)+" UNKNOWN WORD "+IntToStr(tActualWord)+" AT "+IntToStr(_nEvents));
        if (Basis::debugSet())
          printInterpretedWords(pDataWords, pNdataWords, iWord-10, iWord+250);
      }
		}

		if (tBCIDerror){	//tBCIDerror is raised if BCID is not increasing by 1, most likely due to incomplete data transmission, so start new event, actual word is data header here
      if(Basis::warningSet())
        warning("interpretRawData "+IntToStr(_nDataWords)+" BCID ERROR, event "+IntToStr(_nEvents));
      if (Basis::debugSet())
          printInterpretedWords(pDataWords, pNdataWords, iWord-50, iWord+50);
      addEvent();
			_nIncompleteEvents++;
      getTimefromDataHeader(tActualWord, tActualLVL1ID, tStartBCID);
			tNdataHeader = 1;									//tNdataHeader is already 1, because actual word is first data of new event
			tStartBCID = tActualBCID;
			tStartLVL1ID = tActualLVL1ID;
		}
	}
  ////save last incomplete event, otherwise maybe hit buffer/hit array overflow in next chunk
  //storeEventHits();
  //tHitBufferIndex = 0;
	return true;
}

void Interpret::setMetaWordIndex(const unsigned int& tLength, MetaInfo* &rMetaInfo)
{
  _metaInfo = rMetaInfo;
  //sanity check
  for(unsigned int i = 0; i < tLength-1; ++i){
    if(_metaInfo[i].startIndex + _metaInfo[i].length != _metaInfo[i].stopIndex)
      throw 10;
    if(_metaInfo[i].stopIndex != _metaInfo[i+1].startIndex)
      throw 10;
  }
  if(_metaInfo[tLength-1].startIndex + _metaInfo[tLength-1].length != _metaInfo[tLength-1].stopIndex)
    throw 10;

  _metaEventIndexLength = tLength;
  allocateMetaEventIndexArray();
  for(unsigned int i= 0; i<_metaEventIndexLength; ++i)
    _metaEventIndex[i] = 0;
  _metaDataSet = true;
}

void Interpret::getMetaEventIndex(unsigned int& rEventNumberIndex, unsigned long*& rEventNumber)
{
  rEventNumberIndex = _metaEventIndexLength;
  rEventNumber = _metaEventIndex;
}

void Interpret::getHits(unsigned int &rNhits, HitInfo* &rHitInfo)
{
  rHitInfo = _hitInfo;
  rNhits = _hitIndex;
}

void Interpret::resetCounters()
{
  _nDataWords = 0;
  _nTriggers = 0;
	_nEvents = 0;
	_nIncompleteEvents = 0;
	_nDataRecords = 0;
  _nDataHeaders = 0;
  _nServiceRecords = 0;
  _nUnknownWords = 0;
  _nHits = 0;
  _nEmptyEvents = 0;
  _nMaxHitsPerEvent = 0;
  _firstTriggerNrSet = false;
  _lastTriggerNumber = 0;
  resetTriggerErrorCounterArray();
  resetErrorCounterArray();
  resetServiceRecordCounterArray();
}

void Interpret::resetEventVariables()
{
	tNdataHeader = 0;
	tNdataRecord = 0;
	tDbCID = 0;
  tTriggerError = 0;
  tErrorCode = 0;
  tServiceRecord = 0;		
	tBCIDerror = false;
	tLVL1IDisConst = true;
  tTriggerWord = 0;
  tTriggerNumber = 0;
  tStartBCID = 0;
  tStartLVL1ID = 0;
  tHitBufferIndex = 0;
  tTotalHits = 0;
}

void Interpret::setNbCIDs(const unsigned int& NbCIDs)
{
	_NbCID = NbCIDs;
}

void Interpret::setMaxTot(const unsigned int& rMaxTot)
{
	_maxTot = rMaxTot;
}

void Interpret::getServiceRecordsCounters(unsigned int& rNserviceRecords, unsigned long*& rServiceRecordsCounter)
{
  rServiceRecordsCounter = _serviceRecordCounter;
  rNserviceRecords = __NSERVICERECORDS;
}

void Interpret::getErrorCounters(unsigned int& rNerrorCounters, unsigned long*& rErrorCounter)
{
  rErrorCounter = _errorCounter;
  rNerrorCounters = __N_ERROR_CODES;
}

void Interpret::getTriggerErrorCounters(unsigned int& rNTriggerErrorCounters, unsigned long*& rTriggerErrorCounter)
{
  rTriggerErrorCounter = _triggerErrorCounter;
  rNTriggerErrorCounters = __TRG_N_ERROR_CODES;
}

unsigned long Interpret::getNwords()
{
  return _nDataWords;
}

void Interpret::printSummary()
{
    std::cout<<"#Data Words "<<_nDataWords<<"\n";
    std::cout<<"#Data Header "<<_nDataHeaders<<"\n";
    std::cout<<"#Data Records "<<_nDataRecords<<"\n";
    std::cout<<"#Service Records "<<_nServiceRecords<<"\n";
    std::cout<<"#Unknown words "<<_nUnknownWords<<"\n\n";

    std::cout<<"#Hits "<<_nHits<<"\n";
    std::cout<<"MaxHitsPerEvent "<<_nMaxHitsPerEvent<<"\n";
    std::cout<<"#Events "<<_nEvents<<"\n";
    std::cout<<"#Trigger "<<_nTriggers<<"\n\n";
    std::cout<<"#Empty Events "<<_nEmptyEvents<<"\n";
    std::cout<<"#Incomplete Events "<<_nIncompleteEvents<<"\n\n";

    std::cout<<"#ErrorCounters \n";
    std::cout<<"\t0\t"<<_errorCounter[0]<<"\tEvents with SR\n";
    std::cout<<"\t1\t"<<_errorCounter[1]<<"\tEvents with no trigger word\n";
    std::cout<<"\t2\t"<<_errorCounter[2]<<"\tEvents with LVLID non const.\n";
    std::cout<<"\t3\t"<<_errorCounter[3]<<"\tEvents that are incomplete (# BCIDs wrong)\n";
    std::cout<<"\t4\t"<<_errorCounter[4]<<"\tEvents with unknown words\n";
    std::cout<<"\t5\t"<<_errorCounter[5]<<"\tEvents with jumping BCIDs\n";
    std::cout<<"\t6\t"<<_errorCounter[6]<<"\tEvents with TLU trigger error\n";

    std::cout<<"#TriggerErrorCounters \n";
    std::cout<<"\t0\t"<<_triggerErrorCounter[0]<<"\tTrigger number does not increase by 1\n";
    std::cout<<"\t1\t"<<_triggerErrorCounter[1]<<"\t# Trigger per event > 1\n";
    std::cout<<"\t2\t"<<_triggerErrorCounter[2]<<"\tTLU trigger accept error\n";
    std::cout<<"\t3\t"<<_triggerErrorCounter[3]<<"\tTLU low time out error\n";

    std::cout<<"#ServiceRecords \n";
    for(unsigned int i = 0; i<__NSERVICERECORDS; ++i)
      std::cout<<"\t"<<i<<"\t"<<_serviceRecordCounter[i]<<"\n";
}

void Interpret::printHits(const unsigned int& pNhits)
{
  if(pNhits>__MAXARRAYSIZE)
    return;
  std::cout<<"Event\tRelBCID\tTrigger\tLVL1ID\tCol\tRow\tTot\tBCID\tSR\tEventStatus\n";
  for(unsigned int i = 0; i < pNhits; ++i)
    std::cout<<_hitInfo[i].eventNumber<<"\t"<<(unsigned int) _hitInfo[i].relativeBCID<<"\t"<<(unsigned int) _hitInfo[i].triggerNumber<<"\t"<<_hitInfo[i].LVLID<<"\t"<<(unsigned int) _hitInfo[i].column<<"\t"<<_hitInfo[i].row<<"\t"<<(unsigned int) _hitInfo[i].tot<<"\t"<<_hitInfo[i].BCID<<"\t"<<(unsigned int) _hitInfo[i].serviceRecord<<"\t"<<(unsigned int) _hitInfo[i].eventStatus<<"\n";
}

void Interpret::debugEvents(const unsigned long& rStartEvent, const unsigned long& rStopEvent, const bool& debugEvents)
{
  _debugEvents = debugEvents;
  _startDebugEvent = rStartEvent;
  _stopDebugEvent = rStopEvent;
}

//private

void Interpret::addHit(const unsigned char& pRelBCID, const unsigned short int& pLVLID, const unsigned char& pColumn, const unsigned short int& pRow, const unsigned char& pTot, const unsigned short int& pBCID)	//add hit with event number, column, row, relative BCID [0:15], tot, trigger ID
{
  tTotalHits++;
  if(tHitBufferIndex < __MAXHITBUFFERSIZE){
    _hitBuffer[tHitBufferIndex].eventNumber = _nEvents;
    _hitBuffer[tHitBufferIndex].triggerNumber = tTriggerNumber;
    _hitBuffer[tHitBufferIndex].relativeBCID = pRelBCID;
    _hitBuffer[tHitBufferIndex].LVLID = pLVLID;
    _hitBuffer[tHitBufferIndex].column = pColumn;
    _hitBuffer[tHitBufferIndex].row = pRow;
    _hitBuffer[tHitBufferIndex].tot = pTot;
    _hitBuffer[tHitBufferIndex].BCID = pBCID;
    _hitBuffer[tHitBufferIndex].serviceRecord = tServiceRecord;
    _hitBuffer[tHitBufferIndex].triggerStatus = tTriggerError;
    _hitBuffer[tHitBufferIndex].eventStatus = tErrorCode;
    tHitBufferIndex++;
  }
  else{
    if(Basis::errorSet())
      error("addHit: tHitBufferIndex = "+IntToStr(tHitBufferIndex), __LINE__);
    throw 12;
  }
}

void Interpret::storeHit(HitInfo& rHit)
{
  _nHits++;
  if(_hitIndex < __MAXARRAYSIZE){
    _hitInfo[_hitIndex] = rHit;
    //_hitInfo[_hitIndex].eventNumber = _nEvents;
    //_hitInfo[_hitIndex].triggerNumber = tTriggerNumber;
    //_hitInfo[_hitIndex].relativeBCID = pRelBCID;
    //_hitInfo[_hitIndex].LVLID = pLVLID;
    //_hitInfo[_hitIndex].column = pColumn;
    //_hitInfo[_hitIndex].row = pRow;
    //_hitInfo[_hitIndex].tot = pTot;
    //_hitInfo[_hitIndex].BCID = pBCID;
    //_hitInfo[_hitIndex].serviceRecord = tServiceRecord;
    //_hitInfo[_hitIndex].eventStatus = tErrorCode;
    _hitIndex++;
  }
  else{
    if(Basis::errorSet())
      error("storeHit: _hitIndex = "+IntToStr(_hitIndex), __LINE__);
    throw 11;
  }
}

void Interpret::addEvent()
{
  if(Basis::debugSet()){
    std::stringstream tDebug;
    tDebug<<"addEvent() "<<_nEvents;
    debug(tDebug.str());
  }
  if(tTriggerWord == 0){
    addEventErrorCode(__NO_TRG_WORD);
    if(Basis::infoSet())
      info(std::string("addEvent: no trigger word"));
  }
  if(tTriggerWord > 1){
    addTriggerErrorCode(__TRG_NUMBER_MORE_ONE);
    if(Basis::warningSet())
      warning(std::string("addEvent: # trigger words > 1"));
  }
  storeEventHits();
  if(tTotalHits > _nMaxHitsPerEvent)
    _nMaxHitsPerEvent = tTotalHits;
  histogramTriggerErrorCode();
  histogramErrorCode();
  _nEvents++;
	resetEventVariables();
}

void Interpret::storeEventHits()
{
  for (unsigned int i = 0; i<tHitBufferIndex; ++i){
    _hitBuffer[i].triggerNumber = tTriggerNumber; //not needed if trigger number is at the beginning
    _hitBuffer[i].triggerStatus = tTriggerError;
    _hitBuffer[i].eventStatus = tErrorCode;
    storeHit(_hitBuffer[i]);
  }
}

void Interpret::correlateMetaWordIndex(const unsigned long& pEventNumer, const unsigned long& pDataWordIndex)
{
  if(_metaDataSet && pDataWordIndex == _lastWordIndexSet){
     _metaEventIndex[_lastMetaIndexNotSet] = pEventNumer;
     _lastWordIndexSet = _metaInfo[_lastMetaIndexNotSet].stopIndex;
     _lastMetaIndexNotSet++;
  }
}

bool Interpret::getTimefromDataHeader(const unsigned int& pSRAMWORD, unsigned int& pLVL1ID, unsigned int& pBCID)
{
	if (DATA_HEADER_MACRO(pSRAMWORD)){
		if (_fEI4B){
			pLVL1ID = DATA_HEADER_LV1ID_MACRO_FEI4B(pSRAMWORD);
			pBCID = DATA_HEADER_BCID_MACRO_FEI4B(pSRAMWORD);
		}
		else{
			pLVL1ID = DATA_HEADER_LV1ID_MACRO(pSRAMWORD);
			pBCID = DATA_HEADER_BCID_MACRO(pSRAMWORD);
		}
		return true;
	}
	return false;
}

bool Interpret::isDataRecord(const unsigned int& pSRAMWORD)
{
	if (DATA_RECORD_MACRO(pSRAMWORD))
		return true;
	return false;
}

bool Interpret::getHitsfromDataRecord(const unsigned int& pSRAMWORD, int& pColHit1, int& pRowHit1, int& pTotHit1, int& pColHit2, int& pRowHit2, int& pTotHit2)
{
	//if (DATA_RECORD_MACRO(pSRAMWORD)){	//SRAM word is data record
		//check if the hit values are reasonable
		if ((DATA_RECORD_TOT1_MACRO(pSRAMWORD) == 0xF) || (DATA_RECORD_COLUMN1_MACRO(pSRAMWORD) < RAW_DATA_MIN_COLUMN) || (DATA_RECORD_COLUMN1_MACRO(pSRAMWORD) > RAW_DATA_MAX_COLUMN) || (DATA_RECORD_ROW1_MACRO(pSRAMWORD) < RAW_DATA_MIN_ROW) || (DATA_RECORD_ROW1_MACRO(pSRAMWORD) > RAW_DATA_MAX_ROW)){
      warning(std::string("getHitsfromDataRecord: data record values (1. Hit) out of bounds"));
			return false;			
		}
    if ((DATA_RECORD_TOT2_MACRO(pSRAMWORD) != 0xF) && ((DATA_RECORD_COLUMN2_MACRO(pSRAMWORD) < RAW_DATA_MIN_COLUMN) || (DATA_RECORD_COLUMN2_MACRO(pSRAMWORD) > RAW_DATA_MAX_COLUMN) || (DATA_RECORD_ROW2_MACRO(pSRAMWORD) < RAW_DATA_MIN_ROW) || (DATA_RECORD_ROW2_MACRO(pSRAMWORD) > RAW_DATA_MAX_ROW))){
      warning(std::string("getHitsfromDataRecord: data record values (2. Hit) out of bounds"));
			return false;	
    }

		//set first hit values
		if (DATA_RECORD_TOT1_MACRO(pSRAMWORD) <= _maxTot){	//ommit late/small hit and no hit TOT values for the TOT(1) hit
			pColHit1 = DATA_RECORD_COLUMN1_MACRO(pSRAMWORD);
			pRowHit1 = DATA_RECORD_ROW1_MACRO(pSRAMWORD);
			pTotHit1 = DATA_RECORD_TOT1_MACRO(pSRAMWORD);
		}

		//set second hit values
		if (DATA_RECORD_TOT2_MACRO(pSRAMWORD) <= _maxTot){	//ommit late/small hit and no hit (15) tot values for the TOT(2) hit
			pColHit2 = DATA_RECORD_COLUMN2_MACRO(pSRAMWORD);
			pRowHit2 = DATA_RECORD_ROW2_MACRO(pSRAMWORD);
			pTotHit2 = DATA_RECORD_TOT2_MACRO(pSRAMWORD);
		}
		return true;
	//}
	//return false;
}

bool Interpret::getInfoFromServiceRecord(const unsigned int& pSRAMWORD, unsigned int& pSRcode, unsigned int& pSRcount)
{
  if(SERVICE_RECORD_MACRO(pSRAMWORD)){
		pSRcode = SERVICE_RECORD_CODE_MACRO(pSRAMWORD);
		pSRcount = SERVICE_RECORD_COUNTER_MACRO(pSRAMWORD);
    return true;
  }
  return false;
}

bool Interpret::isTriggerWord(const unsigned int& pSRAMWORD)
{
	if (TRIGGER_WORD_MACRO_NEW(pSRAMWORD))	//data word is trigger word
		return true;
	return false;
}

bool Interpret::isOtherWord(const unsigned int& pSRAMWORD)
{
	if (EMPTY_RECORD_MACRO(pSRAMWORD) || ADDRESS_RECORD_MACRO(pSRAMWORD) || VALUE_RECORD_MACRO(pSRAMWORD))
		return true;
	return false;
}

void Interpret::addTriggerErrorCode(const unsigned char& pErrorCode)
{
  if(Basis::debugSet()){
    std::stringstream tDebug;
    tDebug<<"addTriggerErrorCode: "<<(unsigned int) pErrorCode<<"\n";
    debug(tDebug.str());
  }
  addEventErrorCode(__TRG_ERROR);
  tTriggerError |= pErrorCode;
}

void Interpret::addEventErrorCode(const unsigned char& pErrorCode)
{
  if(Basis::debugSet()){
    std::stringstream tDebug;
    tDebug<<"addEventErrorCode: "<<(unsigned int) pErrorCode<<"\n";
    debug(tDebug.str());
  }
  tErrorCode |= pErrorCode;
}

void Interpret::histogramTriggerErrorCode()
{
  unsigned int tBitPosition = 0;
  for(unsigned char iErrorCode = tTriggerError; iErrorCode != 0; iErrorCode = iErrorCode>>1){
    if(iErrorCode & 0x1)
      _triggerErrorCounter[tBitPosition]+=1;
    tBitPosition++;
  }
}

void Interpret::histogramErrorCode()
{
  unsigned int tBitPosition = 0;
  for(unsigned char iErrorCode = tErrorCode; iErrorCode != 0; iErrorCode = iErrorCode>>1){
    if(iErrorCode & 0x1)
      _errorCounter[tBitPosition]+=1;
    tBitPosition++;
  }
}

void Interpret::addServiceRecord(const unsigned char& pSRcode)
{
  tServiceRecord |= pSRcode;
  if(pSRcode<__NSERVICERECORDS)
    _serviceRecordCounter[pSRcode]+=1;
}

void Interpret::allocateHitInfoArray()
{
  _hitInfo = new HitInfo[__MAXARRAYSIZE];
}

void Interpret::deleteHitInfoArray()
{
  if (_hitInfo == 0)
    return;
  delete[] _hitInfo;
  _hitInfo = 0;
}

void Interpret::allocateHitBufferArray()
{
  _hitBuffer = new HitInfo[__MAXHITBUFFERSIZE];
}

void Interpret::deleteHitBufferArray()
{
  if (_hitBuffer == 0)
    return;
  delete[] _hitBuffer;
  _hitBuffer = 0;
}

void Interpret::allocateMetaEventIndexArray()
{
  _metaEventIndex = new unsigned long[_metaEventIndexLength];
}

void Interpret::deleteMetaEventIndexArray()
{
  if (_metaEventIndex == 0)
    return;
  delete[] _metaEventIndex;
  _metaEventIndex = 0;
}

void Interpret::allocateTriggerErrorCounterArray()
{
  _triggerErrorCounter = new unsigned long[__TRG_N_ERROR_CODES];
}

void Interpret::resetTriggerErrorCounterArray()
{
  for(unsigned int i = 0; i<__TRG_N_ERROR_CODES; ++i)
    _triggerErrorCounter[i] = 0;
}

void Interpret::deleteTriggerErrorCounterArray()
{
  if (_triggerErrorCounter == 0)
    return;
  delete[] _triggerErrorCounter;
  _triggerErrorCounter = 0;
}

void Interpret::allocateErrorCounterArray()
{
  _errorCounter = new unsigned long[__N_ERROR_CODES];
}

void Interpret::resetErrorCounterArray()
{
  for(unsigned int i = 0; i<__N_ERROR_CODES; ++i)
    _errorCounter[i] = 0;
}

void Interpret::deleteErrorCounterArray()
{
  if (_errorCounter == 0)
    return;
  delete[] _errorCounter;
  _errorCounter = 0;
}

void Interpret::allocateServiceRecordCounterArray()
{
  _serviceRecordCounter = new unsigned long[__NSERVICERECORDS];
}

void Interpret::resetServiceRecordCounterArray()
{
  for(unsigned int i = 0; i<__NSERVICERECORDS; ++i)
    _serviceRecordCounter[i] = 0;
}

void Interpret::deleteServiceRecordCounterArray()
{
  if (_serviceRecordCounter == 0)
    return;
  delete[] _serviceRecordCounter;
  _serviceRecordCounter = 0;
}

void Interpret::printInterpretedWords(unsigned int* pDataWords, const unsigned int& rNsramWords, const unsigned int& rStartWordIndex, const unsigned int& rEndWordIndex)
{
  std::cout<<"Interpret::printInterpretedWords\n";
  std::cout<<"rStartWordIndex "<<rStartWordIndex<<"\n";
  std::cout<<"rEndWordIndex "<<rEndWordIndex<<"\n";
  unsigned int tStartWordIndex = 0;
  unsigned int tStopWordIndex = rNsramWords;
  if(rStartWordIndex > 0 && rStartWordIndex < rEndWordIndex)
    tStartWordIndex = rStartWordIndex;
  if(rEndWordIndex < rNsramWords)
    tStopWordIndex = rEndWordIndex;
  for(unsigned int iWord = tStartWordIndex; iWord <= tStopWordIndex; ++iWord){
    unsigned int tActualWord = pDataWords[iWord];
    unsigned int tLVL1 = 0;
    unsigned int tBCID = 0;
    int tcol = 0;
    int trow = 0;
    int ttot = 0;
    int tcol2 = 0;
    int trow2 = 0;
    int ttot2 = 0;
    unsigned int tActualSRcode = 0;
    unsigned int tActualSRcounter = 0;
    if(getTimefromDataHeader(tActualWord, tLVL1, tBCID))
      std::cout<<iWord<<" DH "<<tBCID<<" "<<tLVL1<<"\t";
    else if(getHitsfromDataRecord(tActualWord,tcol, trow, ttot,tcol2, trow2, ttot2))
      std::cout<<iWord<<" DR     "<<tcol<<" "<<trow<<" "<<ttot<<" "<<tcol2<<" "<<trow2<<"  "<<ttot2<<"\t";
    else if(isTriggerWord(tActualWord))
      std::cout<<iWord<<" TRIGGER "<<TRIGGER_NUMBER_MACRO_NEW(tActualWord);
    else if(getInfoFromServiceRecord(tActualWord, tActualSRcode, tActualSRcounter))
      std::cout<<iWord<<" SR "<<tActualSRcode;
    else if(!isOtherWord(tActualWord))	
      std::cout<<iWord<<"\tUNKNOWN "<<tActualWord;
    std::cout<<"\n";
  }
}