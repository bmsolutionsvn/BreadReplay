import '@bmsolutionsvn/design/bm.css';
import '@bmsolutionsvn/design/fonts';
import './app.css';

import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

import { App } from './App';
import { connect } from './api/client';

const params = new URLSearchParams(location.hash.slice(1));
const client = connect(params.get('port') ?? '47820', params.get('token') ?? '');

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <App client={client} />
  </StrictMode>,
);