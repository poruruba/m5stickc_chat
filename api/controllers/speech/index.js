'use strict';

const HELPER_BASE = process.env.HELPER_BASE || '../../helpers/';
const Response = require(HELPER_BASE + 'response');
const TextResponse = require(HELPER_BASE + 'textresponse');

const fetch = require('node-fetch');
const speech = require('@google-cloud/speech');
var AWS = require('aws-sdk');

const client = new speech.SpeechClient();
var polly = new AWS.Polly({apiVersion: '2016-06-10', region: 'ap-northeast-1'});

const USERLOCAL_API_KEY = '【ユーザローカルのAPIキー】';

exports.handler = async (event, context, callback) => {
	var body = JSON.parse(event.body);
	console.log(body);

	if( !body.message )
		throw 'message is not set';

	var wav = Buffer.from(body.message, 'base64');
	// 音声の正規化＋16ビット化
	var norm = normalize_wave8(wav);

	// 音声認識
	var ret = await speech_recognize(norm);
	console.log(ret);
	if( ret.length < 1 )
		throw 'recognition failed';

	// AI Chat
	var ret2 = await speech_talk(ret[0]);
	console.log(ret2);

	// 音声合成
	var ret3 = await speech_to_wave(ret2);
	console.log(ret3);

	// 16ビットから8ビットに変換
	var res = speech_wave16_to_wave8(ret3);
	console.log(res);

	return new TextResponse("text/plain", res.toString('base64'));
//	return new TextResponse("text/plain", body.message); // echoback
};

function speech_wave16_to_wave8(wav){
	var buffer = Buffer.alloc(wav.length / 2);
	for( var i = 0 ; i < buffer.length ; i++ ){
		buffer[i] = Math.floor(wav.readInt16LE(i * 2) / 256 + 128);
	}

	return buffer;
}

function normalize_wave8(wav, out_bitlen = 16){
	var sum = 0;
	var max = 0;
	var min = 256;
	for( var i = 0 ; i < wav.length ; i++ ){
		var val = wav[i];
		if( val > max ) max = val;
		if( val < min ) min = val;
		sum += val;
	}

	var average = sum / wav.length;
	var amplitude = Math.max(max - average, average - min);
/*
	console.log('sum=' + sum);
	console.log('avg=' + average);
	console.log('amp=' + amplitude);
	console.log('max=' + max);
	console.log('min=' + min);
*/
	if( out_bitlen == 8 ){
		const norm = Buffer.alloc(wav.length);
		for( var i = 0 ; i < wav.length ; i++ ){
			var value = (wav[i] - average) / amplitude * (127 * 0.8) + 128;
			norm[i] = Math.floor(value);
		}
		return norm;
	}else{
		const norm = Buffer.alloc(wav.length * 2);
		for( var i = 0 ; i < wav.length ; i++ ){
			var value = (wav[i] - average) / amplitude * (32767 * 0.8);
			norm.writeInt16LE(Math.floor(value), i * 2);
		}
		return norm;
	}
}

async function speech_to_wave(message){
	const pollyParams = {
		OutputFormat: 'pcm', // 音声フォーマット
		Text: message,
		VoiceId: 'Mizuki',
		TextType: 'text',
		SampleRate : '8000',
	};
	
	return new Promise((resolve, reject) =>{
		polly.synthesizeSpeech(pollyParams, (err, data) =>{
			if( err ){
				console.log(err);
				return reject(err);
			}
			var buffer = new Buffer(data.AudioStream);
			return resolve(buffer);
		});
	});
}

async function speech_talk(message){
	var body = {
		message: message,
		key: USERLOCAL_API_KEY,
	};
	return do_post('https://chatbot-api.userlocal.jp/api/chat', body)
	.then(json =>{
			return json.result;
	});
}

function do_post(url, body){
  return fetch(url, {
      method : 'POST',
      body : JSON.stringify(body),
      headers: { "Content-Type" : "application/json; charset=utf-8" } 
  })
  .then((response) => {
      if(!response.ok)
          throw "status is not 200.";
      return response.json();
  });
}

async function speech_recognize(wav){
	const config = {
		encoding: 'LINEAR16',
		sampleRateHertz: 8192,
		languageCode: 'ja-JP',
	};
	const audio = {
		content: wav.toString('base64')
	};
	
	const request = {
		config: config,
		audio: audio,
	};
	
	return client.recognize(request)
	.then(response =>{
		const transcription = [];
		for( var i = 0 ; i < response[0].results.length ; i++ )
			transcription.push(response[0].results[i].alternatives[0].transcript);

			return transcription;
	});
}