import CodeRenderer from 'components/code-renderer/code-renderer';
import {Spinner} from 'components/spinner/spinner';
// @ts-ignore : TS does not like image files.
import * as logoImage from 'images/new-logo.svg';
import * as React from 'react';
import {Query} from 'react-apollo';

import Button from '@material-ui/core/Button';
import Card from '@material-ui/core/Card';
import {createStyles, makeStyles, Theme} from '@material-ui/core/styles';

const useStyles = makeStyles((theme: Theme) => {
  return createStyles({
    dialog: {
      width: '700px',
      height: '60%',
    },
    content: {
      padding: theme.spacing(6),
      color: '#B2B5BB',
    },
    container: {
      display: 'flex',
      justifyContent: 'center',
      alignItems: 'center',
      width: '100%',
    },
    header: {
      ...theme.typography.h5,
      color: theme.palette.foreground.two,
    },
    instructions: {
      marginTop: theme.spacing(3),
      ...theme.typography.body1,
      fontFamily: 'monospace',
    },
    linksHeader: {
      ...theme.typography.body1,
      color: theme.palette.foreground.two,
      marginTop: theme.spacing(5),
      marginBottom: theme.spacing(2),
    },
    listItem: {
      marginBottom: theme.spacing(1),
      '&::before': {
        content: '"-"',
        marginRight: theme.spacing(0.5),
      },
    },
    linkItem: {
      ...theme.typography.body1,
      fontFamily: 'monospace',
      color: '#B2B5BB',
      textDecoration: 'underline',
    },
    list: {
      listStyle: 'none',
      paddingLeft: 0,
    },
    buttons: {
      display: 'flex',
      flex: 1,
      justifyContent: 'center',
      marginTop: theme.spacing(6),
      marginBottom: theme.spacing(1),
    },
    button: {
      margin: theme.spacing(3),
    },
    logo: {
      float: 'right',
      marginRight: theme.spacing(2),
      marginBottom: theme.spacing(2),
    },
    centered: {
      display: 'flex',
      alignItems: 'center',
      flexDirection: 'column',
    },
  });
});

export const DeployInstructions = () => {
  const classes = useStyles();

  return (<div className={classes.container}>
    <Card className={classes.dialog}>
      <div className={classes.content}>
        <span className={classes.header}>Install Pixie</span>
        <CodeRenderer
          code={'bash -c "$(curl -fsSL ' + window.location.origin + '/install.sh)"'}
          language='bash'
        />
        <div className={classes.instructions}>
          Run this in a macOS Terminal or Linux shell to install Pixie in your K8s cluster.
          Share with your admin if you don't have access.
        </div>
        <div className={classes.linksHeader}>Don't have K8s?</div>
        <ul className={classes.list}>
          <li className={classes.listItem}>
            <a className={classes.linkItem} href='/docs/install-guides'>Set up a quick local K8s sandbox</a>
          </li>
          <li className={classes.listItem}>
            <a className={classes.linkItem} href='/docs/getting-started/quick-start'>Set up a demo app</a>
          </li>
        </ul>
        <div className={classes.buttons}>
          <Button className={classes.button} href='https://slackin.withpixie.ai/'
            variant='outlined' color='primary' size='large'>
            Slack
          </Button>
          <Button className={classes.button} href='/docs' variant='outlined' color='primary' size='large'>
            Docs
          </Button>
          <Button className={classes.button} href='https://github.com/pixie-labs/pixie'
            variant='outlined' color='primary' size='large'>
            Github
          </Button>
        </div>
      </div>
      <img className={classes.logo} src={logoImage} style={{ width: '55px' }} />
    </Card>
  </div>);
};

interface ClusterInstructionsProps {
  message: string;
}

export const ClusterInstructions = (props: ClusterInstructionsProps) => {
  const classes = useStyles();

  return (<div className={classes.container}>
    <Card className={classes.dialog}>
      <div className={classes.content}>
        <div className={classes.centered}>
          <p>{props.message}</p>
          <Spinner variant='light' />
        </div>
      </div>
      <img className={classes.logo} src={logoImage} style={{ width: '55px' }} />
    </Card>
  </div>);
}
